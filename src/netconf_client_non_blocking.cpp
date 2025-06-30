#include "netconf_client.hpp"
#include "notification_reactor_manager.hpp"
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
#include <errno.h>

bool NetconfClient::connect_non_blocking() {
    if (is_connected_) {
        throw NetconfException("Session already exists, possible double connection attempt");
    }

    int rc = 0;
    int user_given_timeout = connect_timeout_; // TO DO: Modify to accept this value from user
    int current_timeout = 0;
    int socket_connect_timeout = user_given_timeout > 10 ? user_given_timeout *100 : 2000;
    auto start_time = std::chrono::steady_clock::now();
    auto connect_timeout = std::chrono::seconds(user_given_timeout);
    try {
        // Initialize a libssh2 session and store it in our RAII wrapper.
        LIBSSH2_SESSION* raw_session = libssh2_session_init();
        if (!raw_session) {
            throw NetconfException("Failed to initialize libssh2 session");
        }
        session_.reset(raw_session);
        libssh2_session_set_blocking(session_.get(), 0);

        // Resolve hostname.
        std::string resolved_ip;
        {
            std::lock_guard<std::mutex> dns_lock(dns_mutex_);
            current_timeout = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    connect_timeout - (std::chrono::steady_clock::now() - start_time)
                ).count()
            );
            if (current_timeout <= 0) {
                throw NetconfConnectionRefused(
                    "Connection failed to " + hostname_ + " try increasing connection timeout"
                );
            }
            resolved_ip = resolve_hostname_non_blocking(hostname_, current_timeout);
            if (resolved_ip.empty()) {
                throw NetconfConnectionRefused("Failed to resolve hostname: " + hostname_);
            }
        }
        resolved_host_ = resolved_ip;
        current_timeout = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                connect_timeout - (std::chrono::steady_clock::now() - start_time)
            ).count()
        );
        if (current_timeout <= 0) {
            throw NetconfConnectionRefused(
                "Connection failed to " + hostname_ + " try increasing connection timeout"
            );
        }
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
        int flags = fcntl(socket_.get(), F_GETFL, 0);
        if (flags < 0 || fcntl(socket_.get(), F_SETFL, flags | O_NONBLOCK) < 0) {
            throw NetconfException("Failed to set non-blocking mode: " + std::string(strerror(errno)));
        }
        // Prepare server address.
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        if (inet_pton(AF_INET, resolved_ip.c_str(), &server_addr.sin_addr) <= 0) {
            throw NetconfConnectionRefused("Invalid IP address: " + resolved_ip);
        }
        rc = ::connect(socket_.get(), reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
        if (rc < 0 && errno != EINPROGRESS) {
            throw NetconfConnectionRefused("Connection failed: " + std::string(strerror(errno)));
        }
        // Wait for TCP connection completion.
        struct pollfd pfd{};
        pfd.fd = socket_.get();
        pfd.events = POLLOUT;
        int poll_result = poll(
            &pfd,
            1,
            socket_connect_timeout
        );
        if (poll_result <= 0) {
            throw NetconfConnectionRefused(poll_result == 0 ?
                "Unable to open socket for " + hostname_ + " " : "Poll error: " + std::string(strerror(errno)));
        }
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(socket_.get(), SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            throw NetconfConnectionRefused("Connection failed: " +
                std::string(error != 0 ? strerror(error) : strerror(errno)));
        }

        // Perform the SSH handshake.
        struct pollfd session_pfd{};
        session_pfd.fd = socket_.get();
        session_pfd.events = POLLIN | POLLOUT;

        rc = LIBSSH2_ERROR_EAGAIN;
        current_timeout = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                connect_timeout - (std::chrono::steady_clock::now() - start_time)
            ).count()
        );
        if (current_timeout <= 0) {
            throw NetconfConnectionRefused(
                "Connection failed to " + hostname_ + " try increasing connection timeout"
            );
        }
        auto handshake_start_time = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - handshake_start_time < std::chrono::seconds(current_timeout)) {
            int poll_result = poll(&session_pfd, 1, 100);
            if (poll_result < 0) {
                throw NetconfConnectionRefused("Poll error during handshake: " + std::string(strerror(errno)));
            }
            if (poll_result == 0) {
                continue;
            }
            {
                std::lock_guard<std::mutex> ssh_lock(ssh_mutex_);
                rc = libssh2_session_handshake(session_.get(), socket_.get());
            }
            if (rc == 0) {
                break; // Handshake successful.
            }
            if (rc != LIBSSH2_ERROR_EAGAIN) {
                char* err_msg = nullptr;
                libssh2_session_last_error(session_.get(), &err_msg, nullptr, 0);
                throw NetconfConnectionRefused("SSH handshake failed: " +
                    std::string(err_msg ? err_msg : "Unknown error"));
            }
        }
        if (rc != 0) {
            throw NetconfConnectionRefused("SSH handshake timed out");
        }

        rc = LIBSSH2_ERROR_EAGAIN;
        current_timeout = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                connect_timeout - (std::chrono::steady_clock::now() - start_time)
            ).count()
        );
        if (current_timeout <= 0) {
            throw NetconfConnectionRefused(
                "Connection failed to " + hostname_ + " try increasing connection timeout"
            );
        }
        auto auth_start_time = std::chrono::steady_clock::now();
        while (rc == LIBSSH2_ERROR_EAGAIN &&
                std::chrono::steady_clock::now() - auth_start_time < std::chrono::seconds(current_timeout)) {
            // Wait until the socket is ready.
            int poll_ret = poll(&pfd, 1, 300);
            if (poll_ret < 0) {
                throw NetconfAuthError("Poll error during authentication: " + std::string(strerror(errno)));
            }
            rc = libssh2_userauth_password(session_.get(), username_.c_str(), password_.c_str());
        }
        if (rc) {
            char* err_msg = nullptr;
            libssh2_session_last_error(session_.get(), &err_msg, nullptr, 0);
            throw NetconfAuthError("Authentication failed: " +
                std::string(err_msg ? err_msg : "Unknown error"));
        }
        current_timeout = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                connect_timeout - (std::chrono::steady_clock::now() - start_time)
            ).count()
        );
        if (current_timeout <= 0) {
            throw NetconfConnectionRefused(
                "Connection failed to " + hostname_ + " try increasing connection timeout"
            );
        }
        auto channel_start_time = std::chrono::steady_clock::now();
        LIBSSH2_CHANNEL* raw_channel = nullptr;
        while (std::chrono::steady_clock::now() - channel_start_time < std::chrono::seconds(current_timeout)) {
            raw_channel = libssh2_channel_open_session(session_.get());
            if (raw_channel) {
                break; // Channel successfully opened.
            }
            // Check error: if not EAGAIN, break out and throw error.
            int err = libssh2_session_last_error(session_.get(), nullptr, nullptr, 0);
            if (err != LIBSSH2_ERROR_EAGAIN) {
                break;
            }
            // Sleep a bit before retrying.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!raw_channel) {
            throw NetconfChannelError("Failed to create channel for NETCONF");
        }
        channel_.reset(raw_channel);

        rc = LIBSSH2_ERROR_EAGAIN;
        current_timeout = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                connect_timeout - (std::chrono::steady_clock::now() - start_time)
            ).count()
        );
        if (current_timeout <= 0) {
            throw NetconfConnectionRefused(
                "Connection failed to " + hostname_ + " try increasing connection timeout"
            );
        }
        auto subsystem_start_time = std::chrono::steady_clock::now();
        while (rc == LIBSSH2_ERROR_EAGAIN &&
                std::chrono::steady_clock::now() - subsystem_start_time < std::chrono::seconds(current_timeout)) {
            int poll_ret = poll(&pfd, 1, 100);
            if (poll_ret < 0) {
                throw NetconfChannelError("Poll error during channel startup: " + std::string(strerror(errno)));
            }
            rc = libssh2_channel_process_startup(channel_.get(), "subsystem", 9, "netconf", strlen("netconf"));
        }
        if (rc) {
            char* err_msg = nullptr;
            libssh2_session_last_error(session_.get(), &err_msg, nullptr, 0);
            throw NetconfChannelError("Failed to request NETCONF subsystem: " +
                std::string(err_msg ? err_msg : "Unknown error"));
        }

        // Now complete the NETCONF hello exchange.
        std::string server_hello = read_until_eom_non_blocking(channel_.get(), session_.get(), read_timeout_);
        if (server_hello.find("capabilities") != std::string::npos) {
            send_client_hello_non_blocking(channel_.get(), session_.get(), socket_.get());
        } else {
            throw NetconfException("Didn't receive proper NETCONF 'hello' message from device.");
        }
        is_connected_ = true;
        is_blocking_ = false;
        return true;
    }
    catch (const std::exception& err) {
        // RAII wrappers ensure that session_, channel_, and socket_ are cleaned up automatically.
        throw NetconfConnectionRefused("Unable to connect to device: " + std::string(err.what()));
    }
}

bool NetconfClient::connect_notification_non_blocking() {
    if (notif_is_connected_) {
        throw NetconfException("Session already exists, possible double connection attempt");
    }

    int rc = 0;
    int user_given_timeout    = connect_timeout_;
    int current_timeout       = 0;
    int socket_connect_timeout= user_given_timeout > 10
                                ? user_given_timeout * 100
                                : 2000;
    auto start_time    = std::chrono::steady_clock::now();
    auto connect_deadline = std::chrono::seconds(user_given_timeout);

    try {
        // ——— Initialize libssh2 session —————————————
        LIBSSH2_SESSION* raw_session = libssh2_session_init();
        if (!raw_session) {
            throw NetconfException("Failed to initialize libssh2 session");
        }
        notif_session_.reset(raw_session);
        libssh2_session_set_blocking(notif_session_.get(), 0);

        // ——— Resolve hostname in non-blocking fashion —————
        {
            std::lock_guard<std::mutex> dns_lock(dns_mutex_);
            current_timeout = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    connect_deadline - (std::chrono::steady_clock::now() - start_time)
                ).count()
            );
            if (current_timeout <= 0) {
                throw NetconfConnectionRefused("Connection timed out resolving " + hostname_);
            }
            resolved_host_ = resolve_hostname_non_blocking(hostname_, current_timeout);
            if (resolved_host_.empty()) {
                throw NetconfConnectionRefused("Failed to resolve hostname: " + hostname_);
            }
        }

        // ——— Create, configure, and make the socket non-blocking ——
        int raw_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (raw_sock < 0) {
            throw NetconfException("Failed to create socket: " + std::string(strerror(errno)));
        }
        notif_socket_.reset(raw_sock);

        int opt = 1;
        if (setsockopt(notif_socket_.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            throw NetconfException("Failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
        }
        int flags = fcntl(notif_socket_.get(), F_GETFL, 0);
        if (flags < 0
            || fcntl(notif_socket_.get(), F_SETFL, flags | O_NONBLOCK) < 0)
        {
            throw NetconfException("Failed to set non-blocking mode: " + std::string(strerror(errno)));
        }

        // ——— Perform non-blocking TCP connect ——————————————
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port   = htons(port_);
        if (inet_pton(AF_INET, resolved_host_.c_str(), &server_addr.sin_addr) <= 0) {
            throw NetconfConnectionRefused("Invalid IP address: " + resolved_host_);
        }

        rc = ::connect(notif_socket_.get(),
                       reinterpret_cast<struct sockaddr*>(&server_addr),
                       sizeof(server_addr));
        if (rc < 0 && errno != EINPROGRESS) {
            throw NetconfConnectionRefused("Connection failed: " + std::string(strerror(errno)));
        }

        // Wait for TCP connect completion
        struct pollfd pfd{ notif_socket_.get(), POLLOUT, 0 };
        int poll_ret = poll(&pfd, 1, socket_connect_timeout);
        if (poll_ret <= 0) {
            throw NetconfConnectionRefused(
                poll_ret == 0
                ? "Timeout establishing TCP connection"
                : "Poll error: " + std::string(strerror(errno))
            );
        }
        int so_error = 0; socklen_t len = sizeof(so_error);
        if (getsockopt(notif_socket_.get(), SOL_SOCKET, SO_ERROR, &so_error, &len) < 0
            || so_error != 0)
        {
            throw NetconfConnectionRefused(
                "TCP connect failed: " +
                std::string(so_error ? strerror(so_error) : strerror(errno))
            );
        }

        // Perform the SSH handshake.
        struct pollfd session_pfd{};
        session_pfd.fd = notif_socket_.get();
        session_pfd.events = POLLIN | POLLOUT;

        rc = LIBSSH2_ERROR_EAGAIN;
        current_timeout = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                connect_deadline - (std::chrono::steady_clock::now() - start_time)
            ).count()
        );
        if (current_timeout <= 0) {
            throw NetconfConnectionRefused(
                "Connection failed to " + hostname_ + " try increasing connection timeout"
            );
        }
        auto handshake_start_time = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - handshake_start_time < std::chrono::seconds(current_timeout)) {
            int poll_result = poll(&session_pfd, 1, 100);
            if (poll_result < 0) {
                throw NetconfConnectionRefused("Poll error during handshake: " + std::string(strerror(errno)));
            }
            if (poll_result == 0) {
                continue;
            }
            {
                std::lock_guard<std::mutex> ssh_lock(ssh_mutex_);
                rc = libssh2_session_handshake(notif_session_.get(), notif_socket_.get());
            }
            if (rc == 0) {
                break; // Handshake successful.
            }
            if (rc != LIBSSH2_ERROR_EAGAIN) {
                char* err_msg = nullptr;
                libssh2_session_last_error(notif_session_.get(), &err_msg, nullptr, 0);
                throw NetconfConnectionRefused("SSH handshake failed: " +
                    std::string(err_msg ? err_msg : "Unknown error"));
            }
        }
        if (rc != 0) {
            throw NetconfConnectionRefused("SSH handshake timed out");
        }

        rc = LIBSSH2_ERROR_EAGAIN;
        current_timeout = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                connect_deadline - (std::chrono::steady_clock::now() - start_time)
            ).count()
        );
        if (current_timeout <= 0) {
            throw NetconfConnectionRefused(
                "Connection failed to " + hostname_ + " try increasing connection timeout"
            );
        }
        auto auth_start_time = std::chrono::steady_clock::now();
        while (rc == LIBSSH2_ERROR_EAGAIN &&
                std::chrono::steady_clock::now() - auth_start_time < std::chrono::seconds(current_timeout)) {
            // Wait until the socket is ready.
            int poll_ret = poll(&pfd, 1, 300);
            if (poll_ret < 0) {
                throw NetconfAuthError("Poll error during authentication: " + std::string(strerror(errno)));
            }
            rc = libssh2_userauth_password(notif_session_.get(), username_.c_str(), password_.c_str());
        }
        if (rc) {
            char* err_msg = nullptr;
            libssh2_session_last_error(notif_session_.get(), &err_msg, nullptr, 0);
            throw NetconfAuthError("Authentication failed: " +
                std::string(err_msg ? err_msg : "Unknown error"));
        }
        current_timeout = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                connect_deadline - (std::chrono::steady_clock::now() - start_time)
            ).count()
        );
        if (current_timeout <= 0) {
            throw NetconfConnectionRefused(
                "Connection failed to " + hostname_ + " try increasing connection timeout"
            );
        }
        auto channel_start_time = std::chrono::steady_clock::now();
        LIBSSH2_CHANNEL* raw_channel = nullptr;
        while (std::chrono::steady_clock::now() - channel_start_time < std::chrono::seconds(current_timeout)) {
            raw_channel = libssh2_channel_open_session(notif_session_.get());
            if (raw_channel) {
                break; // Channel successfully opened.
            }
            // Check error: if not EAGAIN, break out and throw error.
            int err = libssh2_session_last_error(notif_session_.get(), nullptr, nullptr, 0);
            if (err != LIBSSH2_ERROR_EAGAIN) {
                break;
            }
            // Sleep a bit before retrying.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!raw_channel) {
            throw NetconfChannelError("Failed to create channel for NETCONF");
        }
        notif_channel_.reset(raw_channel);

        rc = LIBSSH2_ERROR_EAGAIN;
        current_timeout = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                connect_deadline - (std::chrono::steady_clock::now() - start_time)
            ).count()
        );
        if (current_timeout <= 0) {
            throw NetconfConnectionRefused(
                "Connection failed to " + hostname_ + " try increasing connection timeout"
            );
        }
        auto subsystem_start_time = std::chrono::steady_clock::now();
        while (rc == LIBSSH2_ERROR_EAGAIN &&
                std::chrono::steady_clock::now() - subsystem_start_time < std::chrono::seconds(current_timeout)) {
            int poll_ret = poll(&pfd, 1, 100);
            if (poll_ret < 0) {
                throw NetconfChannelError("Poll error during channel startup: " + std::string(strerror(errno)));
            }
            rc = libssh2_channel_process_startup(notif_channel_.get(), "subsystem", 9, "netconf", strlen("netconf"));
        }
        if (rc) {
            char* err_msg = nullptr;
            libssh2_session_last_error(notif_session_.get(), &err_msg, nullptr, 0);
            throw NetconfChannelError("Failed to request NETCONF subsystem: " +
                std::string(err_msg ? err_msg : "Unknown error"));
        }

        // Now complete the NETCONF hello exchange.
        std::string server_hello = read_until_eom_non_blocking(
            notif_channel_.get(),
            notif_session_.get(),
            read_timeout_
        );
        if (server_hello.find("capabilities") == std::string::npos) {
            throw NetconfException("Invalid NETCONF <hello> from server");
        }
        send_client_hello_non_blocking(
            notif_channel_.get(),
            notif_session_.get(),
            notif_socket_.get()
        );

        // ——— REGISTER WITH GLOBAL REACTOR ——————————————
        NotificationReactorManager::instance().add(notif_socket_.get(), this);

        notif_is_connected_ = true;
        notif_is_blocking_ = false;
        return true;
    }
    catch (const std::exception& e) {
        throw NetconfConnectionRefused("Unable to connect to device: " + std::string(e.what()));
    }
}

void NetconfClient::on_notification_ready(int fd) {
    auto xml = read_until_eom_non_blocking(
        notif_channel_.get(),
        notif_session_.get(),
        -1
    );
    {
        std::lock_guard<std::mutex> lk(_notif_queue_mtx);
        _notif_queue.push_back(std::move(xml));
    }
    _notif_queue_cv.notify_one();
}

std::string NetconfClient::send_rpc_non_blocking(const std::string& rpc) {
    return send_rpc_non_blocking_func(channel_.get(), session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::next_notification() {
    if (!notif_channel_) {
        throw NetconfException("Notification channel not open.");
    }
    if (!notif_session_) {
        throw NetconfException("Notification session not open.");
    }
    std::unique_lock<std::mutex> lk(_notif_queue_mtx);
    bool got_data = _notif_queue_cv.wait_for(
        lk,
        std::chrono::milliseconds(10),
        [&]{ return !_notif_queue.empty(); }
    );
    if (!got_data) {
        lk.unlock();
        return std::string{};
    }
    std::string xml = std::move(_notif_queue.front());
    _notif_queue.pop_front();
    lk.unlock();
    return xml;
}


bool NetconfClient::is_subscription_active() const {
    if (!notif_is_connected_) return false;
    if (!notif_channel_) return false;
    if (!notif_session_) return false;

    int fd = notif_socket_.get();
    if (fd < 0) return false;

    int flags = fcntl(fd, F_GETFD);
    if (flags < 0 && errno == EBADF) return false;

    return true;
}


std::string NetconfClient::get_non_blocking(const std::string& filter) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<get>)";
    if (!filter.empty()) {
        rpc += R"(<filter type="subtree">)" + filter + "</filter>";
    }
    rpc += R"(</get></rpc>)";
    return send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::get_config_non_blocking(const std::string& source,
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
    return send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::copy_config_non_blocking(const std::string& target,
                                       const std::string& source) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<copy-config>)"
            R"(<target><)" + target + R"(/></target>)"
            R"(<source><)" + source + R"(/></source>)"
          R"(</copy-config>)"
        R"(</rpc>)";
    return send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::delete_config_non_blocking(const std::string& target) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<delete-config>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</delete-config>)"
        R"(</rpc>)";
    return send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::validate_non_blocking(const std::string& source) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<validate>)"
            R"(<source><)" + source + R"(/></source>)"
          R"(</validate>)"
        R"(</rpc>)";
    return send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::edit_config_non_blocking(const std::string& target,
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
    std::string reply = send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
    if (do_validate) {
        validate_non_blocking(target);
    }
    return reply;
}

std::string NetconfClient::subscribe_non_blocking(
    const std::string& stream,
    const std::string& filter) {
        bool connection_status = connect_notification_non_blocking();
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
        return send_rpc_non_blocking_func(notif_channel_.get(),  notif_session_.get(), notif_socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::lock_non_blocking(const std::string& target) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<lock>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</lock>)"
        R"(</rpc>)";
    return send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::unlock_non_blocking(const std::string& target) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<unlock>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</unlock>)"
        R"(</rpc>)";
    return send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::commit_non_blocking() {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<commit/>)"
        R"(</rpc>)";
    return send_rpc_non_blocking_func(channel_.get(), session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::locked_edit_config_non_blocking(const std::string& target,
                                              const std::string& config,
                                              bool do_validate) {
    lock_non_blocking(target);
    std::string reply = edit_config_non_blocking(target, config, do_validate);
    commit_non_blocking();
    unlock_non_blocking(target);
    return reply;
}
