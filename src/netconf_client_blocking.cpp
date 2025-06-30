#include "netconf_client.hpp"
#include "notification_reactor_manager.hpp"
#include "notification_reactor.hpp"
#include <stdexcept>
#include <iostream>
#include <future>
#include <sstream>
#include <libssh2.h>
#include <tinyxml2.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

void NetconfClient::disconnect() {
    // Clean up RPC session
    channel_.reset();
    session_.reset();
    socket_.reset();

    if (notif_channel_) {
        // Unregister notification socket from the global reactor
        NotificationReactorManager::instance().remove(notif_socket_.get());

        // Clean up notification session
        notif_channel_.reset();
        notif_session_.reset();
        notif_socket_.reset();

        notif_is_blocking_   = false;
        notif_is_connected_  = false;
    }

    is_blocking_   = false;
    is_connected_  = false;
}

void NetconfClient::delete_notification_session() {
    if (notif_channel_) {
        NotificationReactorManager::instance().remove(notif_socket_.get());
        notif_channel_.reset();
        notif_session_.reset();
        notif_socket_.reset();
    }
    notif_is_blocking_   = false;
    notif_is_connected_  = false;
}


bool NetconfClient::connect_blocking() {
    if (is_connected_) {
        throw NetconfException("Session already exists, possible double connection attempt");
    }

    int rc = 0;
    auto connect_timeout = std::chrono::seconds(connect_timeout_);
    auto start_time = std::chrono::steady_clock::now();

    try {
        // Initialize a libssh2 session and set it to blocking mode.
        LIBSSH2_SESSION* raw_session = libssh2_session_init();
        if (!raw_session) {
            throw NetconfException("Failed to initialize libssh2 session");
        }
        session_.reset(raw_session);
        libssh2_session_set_blocking(session_.get(), 1);

        // Resolve hostname.
        std::string resolved_ip;
        {
            std::lock_guard<std::mutex> dns_lock(dns_mutex_);
            // Use the full timeout value for DNS resolution.
            resolved_ip = resolve_hostname_blocking(hostname_);
            if (resolved_ip.empty()) {
                throw NetconfConnectionRefused("Failed to resolve hostname: " + hostname_);
            }
        }
        if (std::chrono::steady_clock::now() - start_time > connect_timeout) {
            throw NetconfConnectionRefused("Connection timed out during hostname resolution");
        }
        resolved_host_ = resolved_ip;

        // Create and configure the socket.
        int raw_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (raw_sock < 0) {
            throw NetconfException("Failed to create socket: " + std::string(strerror(errno)));
        }
        socket_.reset(raw_sock);
        int option_value = 1;
        if (setsockopt(socket_.get(), SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value)) < 0) {
            throw NetconfException("Failed to set socket options: " + std::string(strerror(errno)));
        }
        // In blocking mode, do not set the non-blocking flag.
        // Prepare the server address.
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        if (inet_pton(AF_INET, resolved_ip.c_str(), &server_addr.sin_addr) <= 0) {
            throw NetconfConnectionRefused("Invalid IP address: " + resolved_ip);
        }
        // Connect (this call will block).
        rc = ::connect(socket_.get(), reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
        if (rc < 0) {
            throw NetconfConnectionRefused("Connection failed: " + std::string(strerror(errno)));
        }
        if (std::chrono::steady_clock::now() - start_time > connect_timeout) {
            throw NetconfConnectionRefused("Connection timed out during TCP connection");
        }

        // Perform the SSH handshake (blocking call).
        rc = libssh2_session_handshake(session_.get(), socket_.get());
        if (rc) {
            char* err_msg = nullptr;
            libssh2_session_last_error(session_.get(), &err_msg, nullptr, 0);
            throw NetconfConnectionRefused("SSH handshake failed: " +
                std::string(err_msg ? err_msg : "Unknown error"));
        }
        if (std::chrono::steady_clock::now() - start_time > connect_timeout) {
            throw NetconfConnectionRefused("Connection timed out during SSH handshake");
        }

        // Authenticate with password (blocking call).
        rc = libssh2_userauth_password(session_.get(), username_.c_str(), password_.c_str());
        if (rc) {
            char* err_msg = nullptr;
            libssh2_session_last_error(session_.get(), &err_msg, nullptr, 0);
            throw NetconfAuthError("Authentication failed: " +
                std::string(err_msg ? err_msg : "Unknown error"));
        }
        if (std::chrono::steady_clock::now() - start_time > connect_timeout) {
            throw NetconfConnectionRefused("Connection timed out during authentication");
        }

        // Open a channel for NETCONF.
        LIBSSH2_CHANNEL* raw_channel = libssh2_channel_open_session(session_.get());
        if (!raw_channel) {
            throw NetconfChannelError("Failed to create channel for NETCONF");
        }
        channel_.reset(raw_channel);

        // Request the NETCONF subsystem (blocking).
        rc = libssh2_channel_process_startup(channel_.get(), "subsystem", 9, "netconf", strlen("netconf"));
        if (rc) {
            char* err_msg = nullptr;
            libssh2_session_last_error(session_.get(), &err_msg, nullptr, 0);
            throw NetconfChannelError("Failed to request NETCONF subsystem: " +
                std::string(err_msg ? err_msg : "Unknown error"));
        }
        if (std::chrono::steady_clock::now() - start_time > connect_timeout) {
            throw NetconfConnectionRefused("Connection timed out during subsystem startup");
        }

        // Complete the NETCONF hello exchange using the blocking read version.
        std::string server_hello = read_until_eom_blocking(
            channel_.get(),
            session_.get(),
            read_timeout_
        ); // blocking read
        if (server_hello.find("capabilities") != std::string::npos) {
            send_client_hello_blocking(channel_.get(), session_.get());  // blocking write
        } else {
            throw NetconfException("Didn't receive proper NETCONF 'hello' message from device.");
        }
        is_blocking_ = true;
        is_connected_ = true;
        return true;
    }
    catch (const std::exception& err) {
        // RAII wrappers will clean up resources automatically.
        throw NetconfConnectionRefused("Unable to connect to device: " + std::string(err.what()));
    }
}

bool NetconfClient::connect_notification_blocking() {
    if (notif_is_connected_) {
        throw NetconfException("Notification session already exists");
    }

    try {
        // 1. Create a new libssh2_session
        LIBSSH2_SESSION* raw_sess = libssh2_session_init();
        if (!raw_sess) {
            throw NetconfException("Failed to init libssh2 session for notifications");
        }
        notif_session_.reset(raw_sess);
        libssh2_session_set_blocking(notif_session_.get(), 1);

        // 2. Resolve hostname
        std::string resolved_ip = resolve_hostname_blocking(hostname_);
        if (resolved_ip.empty()) {
            throw NetconfConnectionRefused("Failed to resolve hostname for notifications: " + hostname_);
        }

        // 3. Create and connect a new socket
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            throw NetconfException("Failed to create socket for notification session: " + std::string(strerror(errno)));
        }
        notif_socket_.reset(sock);

        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        if (inet_pton(AF_INET, resolved_ip.c_str(), &server_addr.sin_addr) <= 0) {
            throw NetconfConnectionRefused("Invalid IP address: " + resolved_ip);
        }
        if (::connect(notif_socket_.get(), reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
            throw NetconfConnectionRefused("Notification connect() failed: " + std::string(strerror(errno)));
        }

        // 4. SSH handshake
        int rc = libssh2_session_handshake(notif_session_.get(), notif_socket_.get());
        if (rc) {
            char* err = nullptr;
            libssh2_session_last_error(notif_session_.get(), &err, nullptr, 0);
            throw NetconfConnectionRefused("Notification handshake failed: " + std::string(err ? err : ""));
        }

        // 5. Authenticate
        rc = libssh2_userauth_password(notif_session_.get(), username_.c_str(), password_.c_str());
        if (rc) {
            char* err = nullptr;
            libssh2_session_last_error(notif_session_.get(), &err, nullptr, 0);
            throw NetconfAuthError("Notification auth failed: " + std::string(err ? err : ""));
        }

        // 6. Open channel & request netconf subsystem
        LIBSSH2_CHANNEL* raw_ch = libssh2_channel_open_session(notif_session_.get());
        if (!raw_ch) {
            throw NetconfChannelError("Failed to open notification channel");
        }
        notif_channel_.reset(raw_ch);

        rc = libssh2_channel_process_startup(notif_channel_.get(), "subsystem", 9, "netconf", 7);
        if (rc) {
            char* err = nullptr;
            libssh2_session_last_error(notif_session_.get(), &err, nullptr, 0);
            throw NetconfChannelError("Failed to request netconf subsystem for notifications: " + std::string(err ? err : ""));
        }

        // 7. Exchange HELLO (blocking)
        std::string server_hello = read_until_eom_blocking(
            notif_channel_.get(),
            notif_session_.get(),
            read_timeout_
        );  // new function or reuse w/ param
        if (server_hello.find("capabilities") != std::string::npos) {
            send_client_hello_blocking(notif_channel_.get(), notif_session_.get());
        } else {
            throw NetconfException("Notification session: no valid hello from device");
        }
        notif_is_connected_ = true;
        notif_is_blocking_ = true;
        return true;
    }
    catch (const std::exception &ex) {
        notif_session_.reset();
        notif_channel_.reset();
        notif_socket_.reset();
        throw NetconfConnectionRefused("Failed to establish notification session: " + std::string(ex.what()));
    }
}

std::string NetconfClient::send_rpc_blocking(const std::string& rpc) {
    return send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::receive_notification_blocking() {
    if (!notif_channel_) {
        throw NetconfException("Notification channel not open.");
    }
    if (!notif_session_) {
        throw NetconfException("Notification session not open.");
    }
    return read_until_eom_blocking(notif_channel_.get(), notif_session_.get(), read_timeout_);
}

std::string NetconfClient::get_blocking(const std::string& filter) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<get>)";
    if (!filter.empty()) {
        rpc += R"(<filter type="subtree">)" + filter + "</filter>";
    }
    rpc += R"(</get></rpc>)";
    return send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::get_config_blocking(const std::string& source,
    const std::string& filter) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
        R"(<get-config>)"
        R"(<source><)" + source + R"(/></source>)";
    if (!filter.empty()) {
        rpc += R"(<filter type="subtree">)" + filter + "</filter>";
    }
    rpc += R"(</get-config></rpc>)";
    return send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::copy_config_blocking(const std::string& target,
    const std::string& source) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
        R"(<copy-config>)"
        R"(<target><)" + target + R"(/></target>)"
        R"(<source><)" + source + R"(/></source>)"
        R"(</copy-config>)"
        R"(</rpc>)";
    return send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::delete_config_blocking(const std::string& target) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<delete-config>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</delete-config>)"
        R"(</rpc>)";
    return send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::validate_blocking(const std::string& source) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<validate>)"
            R"(<source><)" + source + R"(/></source>)"
          R"(</validate>)"
        R"(</rpc>)";
    return send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::edit_config_blocking(const std::string& target,
    const std::string& config,
    bool do_validate) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
            R"(<edit-config>)"
                R"(<target><)" + target + R"(/></target>)"
                R"(<config>)" + config + R"(</config>)"
            R"(</edit-config>)"
        R"(</rpc>)";
    std::string reply = send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
    if (do_validate) {
        validate_blocking(target);
    }
    return reply;
}

std::string NetconfClient::subscribe_blocking(
    const std::string& stream,
    const std::string& filter) {
    bool connection_status = connect_notification_blocking();
    if (!connection_status) {
        throw NetconfException("Unable to create notifications channel");
    }
    if (!notif_channel_) {
        throw NetconfException("No notifications channel present");
    }
    if (!notif_session_) {
        throw NetconfException("No notifications session present");
    }
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
            R"(<create-subscription xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0">)"
                R"(<stream>)" + stream + R"(</stream>)";
    if (!filter.empty()) {
        rpc += R"(<filter type="subtree">)" + filter + "</filter>";
    }
        rpc += R"(</create-subscription></rpc>)";
    return send_rpc_blocking_func(notif_channel_.get(), notif_session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::lock_blocking(const std::string& target) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<lock>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</lock>)"
        R"(</rpc>)";
    return send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::unlock_blocking(const std::string& target) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<unlock>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</unlock>)"
        R"(</rpc>)";
    return send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::commit_blocking() {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<commit/>)"
        R"(</rpc>)";
    return send_rpc_blocking_func(channel_.get(), session_.get(), rpc, read_timeout_);
}

std::string NetconfClient::locked_edit_config_blocking(const std::string& target,
    const std::string& config,
    bool do_validate) {
    lock_blocking(target);
    std::string reply = edit_config_blocking(target, config, do_validate);
    commit_blocking();
    unlock_blocking(target);
    return reply;
}
