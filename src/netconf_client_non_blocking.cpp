#include "netconf_client.hpp"
#include "notification_event_bus.hpp"
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
#include <algorithm>
#include <cctype>

namespace {
    constexpr const char* NETCONF_NOTIFICATION_EOM = "]]>]]>";
    constexpr std::size_t NETCONF_NOTIFICATION_EOM_LEN = 6;
    constexpr int NOTIFICATION_POLL_SLICE_MS = 1000;

    std::string trim_copy(const std::string& input) {
        auto begin = std::find_if_not(input.begin(), input.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        auto end = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }).base();

        if (begin >= end) {
            return std::string{};
        }

        return std::string(begin, end);
    }

    std::string xml_local_name(const char* name) {
        if (!name) {
            return std::string{};
        }

        std::string value(name);
        const std::size_t colon = value.rfind(':');
        if (colon != std::string::npos) {
            return value.substr(colon + 1);
        }
        return value;
    }

    bool is_valid_notification_document(const std::string& frame_without_eom) {
        const std::string payload = trim_copy(frame_without_eom);
        if (payload.empty()) {
            return false;
        }

        tinyxml2::XMLDocument doc;
        const tinyxml2::XMLError parse_status = doc.Parse(payload.c_str(), payload.size());
        if (parse_status != tinyxml2::XML_SUCCESS) {
            return false;
        }

        tinyxml2::XMLElement* root = doc.RootElement();
        if (!root) {
            return false;
        }

        return xml_local_name(root->Name()) == "notification";
    }

    std::size_t find_notification_start_tag(
        const std::string& data,
        std::size_t from = 0
    ) {
        std::size_t pos = from;

        while (true) {
            pos = data.find('<', pos);
            if (pos == std::string::npos) {
                return std::string::npos;
            }

            if (pos + 1 >= data.size()) {
                return std::string::npos;
            }

            const char next = data[pos + 1];
            if (next == '/' || next == '!' || next == '?') {
                ++pos;
                continue;
            }

            std::size_t name_begin = pos + 1;
            std::size_t name_end = name_begin;
            while (name_end < data.size()) {
                unsigned char c = static_cast<unsigned char>(data[name_end]);
                if (std::isspace(c) || data[name_end] == '>' || data[name_end] == '/') {
                    break;
                }
                ++name_end;
            }

            if (name_end == name_begin) {
                ++pos;
                continue;
            }

            std::string qname = data.substr(name_begin, name_end - name_begin);
            const std::size_t colon = qname.rfind(':');
            const std::string local =
                colon == std::string::npos ? qname : qname.substr(colon + 1);

            if (local == "notification") {
                return pos;
            }

            pos = name_end;
        }
    }

    bool benign_notification_prefix(const std::string& prefix) {
        const std::string trimmed = trim_copy(prefix);
        if (trimmed.empty()) {
            return true;
        }

        if (trimmed.rfind("<?xml", 0) == 0) {
            const std::size_t end_decl = trimmed.find("?>");
            if (end_decl != std::string::npos) {
                return trim_copy(trimmed.substr(end_decl + 2)).empty();
            }
        }

        return false;
    }

    bool notification_end_after_start(
        const std::string& data,
        std::size_t start,
        std::size_t& end_after
    ) {
        if (start == std::string::npos || start + 1 >= data.size()) {
            return false;
        }

        std::size_t name_begin = start + 1;
        std::size_t name_end = name_begin;
        while (name_end < data.size()) {
            unsigned char c = static_cast<unsigned char>(data[name_end]);
            if (std::isspace(c) || data[name_end] == '>' || data[name_end] == '/') {
                break;
            }
            ++name_end;
        }

        if (name_end == name_begin) {
            return false;
        }

        const std::string qname = data.substr(name_begin, name_end - name_begin);
        const std::string close_tag = "</" + qname + ">";
        const std::size_t close_pos = data.find(close_tag, name_end);
        if (close_pos == std::string::npos) {
            return false;
        }

        end_after = close_pos + close_tag.size();
        return true;
    }

    std::string read_available_notification_bytes(
        LIBSSH2_CHANNEL* chan,
        LIBSSH2_SESSION* sess
    ) {
        if (!chan || !sess) {
            throw NetconfException("Notification channel/session is not active");
        }

        std::string bytes;
        char buffer[4096];

        while (true) {
            int nbytes = libssh2_channel_read_nonblocking(
                chan,
                buffer,
                sizeof(buffer),
                0
            );

            if (nbytes > 0) {
                bytes.append(buffer, nbytes);
                continue;
            }

            if (nbytes == 0 || nbytes == LIBSSH2_ERROR_EAGAIN) {
                break;
            }

            char* err_msg = nullptr;
            libssh2_session_last_error(sess, &err_msg, nullptr, 0);
            throw NetconfException(
                "Error reading from notification channel: " +
                std::string(err_msg ? err_msg : "Unknown error")
            );
        }

        return bytes;
    }

    int poll_notification_fd(int fd, int timeout_ms) {
        if (fd < 0) {
            throw NetconfException("Invalid notification socket while waiting for EOM");
        }

        while (true) {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN | POLLPRI;

            int ret = poll(&pfd, 1, timeout_ms);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw NetconfException(
                    "Poll error while waiting for notification EOM: " +
                    std::string(strerror(errno))
                );
            }

            if (ret == 0) {
                return 0;
            }

            if (pfd.revents & POLLNVAL) {
                throw NetconfException("Invalid notification socket while waiting for EOM");
            }
            if (pfd.revents & (POLLERR | POLLHUP)) {
                throw NetconfException("Notification socket closed while waiting for EOM");
            }
            if (pfd.revents & (POLLIN | POLLPRI)) {
                return 1;
            }

            return 0;
        }
    }
}

bool NetconfClient::connect_non_blocking() {
    if (is_connected_) {
        throw NetconfException("Session already exists, possible double connection attempt");
    }

    int rc = 0;
    int user_given_timeout = connect_timeout_; // TO DO: Modify to accept this value from user
    int current_timeout = 0;
    int socket_connect_timeout = socket_connect_timeout_ * 1000 ; // Convert to milliseconds for poll
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
        std::string server_hello = read_until_eom_non_blocking(channel_.get(), session_.get(), socket_.get(), read_timeout_);
        if (server_hello.find("capabilities") != std::string::npos) {
            send_client_hello_non_blocking(channel_.get(), session_.get(), socket_.get());
        } else {
            throw NetconfException("Didn't receive proper NETCONF 'hello' message from device.");
        }
        is_connected_ = true;
        is_blocking_ = false;
        return true;
    } catch (const std::exception& err) {
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
    int socket_connect_timeout= socket_connect_timeout_ * 1000; // Convert to milliseconds for poll
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
            notif_socket_.get(),
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
        // NotificationReactorManager::instance().add(
        //     notif_socket_.get(),
        //     shared_from_this()
        // );
        // UPDATE... I am removing the registration of notification FD with reactor here.
        //
        // At this point I only have an SSH/NETCONF notification channel.
        // The <create-subscription> RPC has not been sent yet.
        //
        // If I register here, the reactor can steal the subscription
        // <rpc-reply> before subscribe_non_blocking() reads it.
        notif_is_connected_ = false;
        notif_is_blocking_ = false;
        return true;
    }
    catch (const std::exception& e) {
        throw NetconfConnectionRefused("Unable to connect to device: " + std::string(e.what()));
    }
}

void NetconfClient::mark_notification_dead() noexcept {
    try {
        {
            std::lock_guard<std::mutex> guard(notif_mutex_);

            notif_is_connected_ = false;
            notif_is_blocking_ = false;

            notif_channel_.reset();
            notif_session_.reset();
            notif_socket_.reset();
        }
        {
            std::lock_guard<std::mutex> lk(_notif_queue_mtx);
            _notif_queue.clear();
            _notif_rx_buffer.clear();
            _notif_rx_partial_timer_active = false;
        }
        _notif_queue_cv.notify_all();
    } catch (...) {
        // Never throw from cleanup called by reactor thread.
    }
}

void NetconfClient::on_notification_ready(int fd) {
    try {
        std::vector<NotificationHealthEvent> events_to_emit;
        std::size_t queued_notifications = 0;

        auto flush_events_and_notifications = [&]() {
            for (auto& event : events_to_emit) {
                NotificationEventBus::instance().emit(std::move(event));
            }
            events_to_emit.clear();

            if (queued_notifications > 0) {
                _notif_queue_cv.notify_all();
                queued_notifications = 0;
            }
        };

        auto add_diagnostic_event_locked = [&](
            const std::string& type,
            const std::string& message,
            std::int64_t partial_bytes,
            bool increment_incomplete_count
        ) {
            if (increment_incomplete_count) {
                ++_notif_incomplete_count;
            }

            events_to_emit.push_back(
                make_notification_health_event_locked(
                    type,
                    message,
                    fd,
                    0,
                    partial_bytes
                )
            );
        };

        auto enqueue_or_drop_locked = [&](std::string notification,
                                          std::int64_t diagnostic_bytes) {
            if (notification.empty()) {
                return;
            }

            if (_notif_queue_max_size_ >= 0 &&
                _notif_queue.size() >= static_cast<size_t>(_notif_queue_max_size_)) {
                ++_notif_dropped_queue_full_count;

                const std::uint64_t dropped_delta =
                    _notif_dropped_queue_full_count - _notif_last_drop_event_count;

                const bool first_queue_full_event = !_notif_queue_full_state;

                if (first_queue_full_event ||
                    dropped_delta >= static_cast<std::uint64_t>(notif_drop_event_threshold_)) {
                    _notif_queue_full_state = true;
                    _notif_last_drop_event_count = _notif_dropped_queue_full_count;

                    events_to_emit.push_back(
                        make_notification_health_event_locked(
                            first_queue_full_event
                                ? "notification_queue_full"
                                : "notification_drops_summary",
                            "Notification queue is full; dropping notifications",
                            fd,
                            static_cast<std::int64_t>(dropped_delta),
                            diagnostic_bytes
                        )
                    );

                    std::cerr
                        << "Notification queue full, dropping notifications. "
                        << "queue_size=" << _notif_queue.size()
                        << " queue_max_size=" << _notif_queue_max_size_
                        << " dropped_queue_full=" << _notif_dropped_queue_full_count
                        << " dropped_delta=" << dropped_delta
                        << std::endl;
                }

                return;
            }

            _notif_queue.push_back(std::move(notification));
            ++queued_notifications;
            ++_notif_enqueued_count;

            if (_notif_queue.size() > _notif_queue_high_watermark) {
                _notif_queue_high_watermark = _notif_queue.size();
            }
        };

        auto process_eom_frame_locked = [&](const std::string& frame_without_eom) {
            const std::string trimmed = trim_copy(frame_without_eom);
            const std::int64_t frame_bytes =
                static_cast<std::int64_t>(frame_without_eom.size());

            if (trimmed.empty()) {
                add_diagnostic_event_locked(
                    "malformed_notification",
                    "Received NETCONF EOM marker without notification payload; dropped empty frame",
                    frame_bytes,
                    false
                );
                return;
            }

            const bool malformed = !is_valid_notification_document(frame_without_eom);
            if (malformed) {
                add_diagnostic_event_locked(
                    "malformed_notification",
                    "Received EOM-delimited data that is not a valid NETCONF notification; queued malformed frame",
                    frame_bytes,
                    false
                );
            }

            enqueue_or_drop_locked(
                frame_without_eom + NETCONF_NOTIFICATION_EOM,
                malformed ? frame_bytes : 0
            );
        };

        auto process_recovered_missing_eom_locked = [&](const std::string& frame_without_eom) {
            const std::int64_t frame_bytes =
                static_cast<std::int64_t>(frame_without_eom.size());

            add_diagnostic_event_locked(
                "malformed_notification",
                "Recovered complete notification XML without NETCONF EOM before the next notification; queued recovered frame",
                frame_bytes,
                false
            );

            enqueue_or_drop_locked(frame_without_eom, frame_bytes);
        };

        auto process_rx_buffer_locked = [&]() {
            bool progressed = true;

            while (progressed) {
                progressed = false;

                if (_notif_rx_buffer.empty()) {
                    _notif_rx_partial_timer_active = false;
                    break;
                }

                const std::size_t first_notification =
                    find_notification_start_tag(_notif_rx_buffer, 0);

                if (first_notification != std::string::npos && first_notification > 0) {
                    const std::string prefix = _notif_rx_buffer.substr(0, first_notification);
                    if (!benign_notification_prefix(prefix)) {
                        add_diagnostic_event_locked(
                            "malformed_notification",
                            "Received orphan notification bytes before a notification start tag; dropped orphan fragment",
                            static_cast<std::int64_t>(prefix.size()),
                            false
                        );
                        _notif_rx_buffer.erase(0, first_notification);
                        _notif_rx_partial_timer_active = false;
                        progressed = true;
                        continue;
                    }
                }

                std::size_t eom_pos = std::string::npos;
                while (
                    (
                        eom_pos = _notif_rx_buffer.find(NETCONF_NOTIFICATION_EOM)
                    ) != std::string::npos
                ) {
                    std::string frame = _notif_rx_buffer.substr(0, eom_pos);
                    _notif_rx_buffer.erase(0, eom_pos + NETCONF_NOTIFICATION_EOM_LEN);
                    _notif_rx_partial_timer_active = false;

                    process_eom_frame_locked(frame);
                    progressed = true;
                }
                if (_notif_rx_buffer.empty()) {
                    _notif_rx_partial_timer_active = false;
                    break;
                }

                std::size_t notification_start = find_notification_start_tag(_notif_rx_buffer, 0);
                if (notification_start != std::string::npos) {
                    std::size_t notification_end = std::string::npos;

                    if (notification_end_after_start(
                            _notif_rx_buffer,
                            notification_start,
                            notification_end
                        )) {
                        const std::size_t next_notification =
                            find_notification_start_tag(_notif_rx_buffer, notification_end);

                        if (next_notification != std::string::npos) {
                            std::string recovered = _notif_rx_buffer.substr(0, notification_end);
                            _notif_rx_buffer.erase(0, notification_end);
                            _notif_rx_partial_timer_active = false;

                            process_recovered_missing_eom_locked(recovered);
                            progressed = true;
                            continue;
                        }
                    } else {
                        const std::size_t next_notification =
                            find_notification_start_tag(_notif_rx_buffer, notification_start + 1);

                        if (next_notification != std::string::npos) {
                            std::string abandoned_partial =
                                _notif_rx_buffer.substr(0, next_notification);

                            _notif_rx_buffer.erase(0, next_notification);
                            _notif_rx_partial_timer_active = false;

                            const std::int64_t partial_bytes =
                                static_cast<std::int64_t>(abandoned_partial.size());

                            add_diagnostic_event_locked(
                                "incomplete_notification",
                                "Received a new notification start before the previous notification was completed; queued abandoned partial notification",
                                partial_bytes,
                                true
                            );

                            enqueue_or_drop_locked(std::move(abandoned_partial), partial_bytes);

                            progressed = true;
                            continue;
                        }
                    }
                }
            }

            if (_notif_rx_buffer.empty()) {
                _notif_rx_partial_timer_active = false;
            } else if (!_notif_rx_partial_timer_active) {
                _notif_rx_partial_timer_active = true;
                _notif_rx_partial_started_at = std::chrono::steady_clock::now();
            }
        };

        auto partial_size_limit_reached_locked = [&]() -> bool {
            if (notif_incomplete_max_kb_ <= 0 || _notif_rx_buffer.empty()) {
                return false;
            }

            const std::size_t max_bytes =
                static_cast<std::size_t>(notif_incomplete_max_kb_) * 1024;
            return _notif_rx_buffer.size() >= max_bytes;
        };

        auto partial_timeout_reached_locked = [&]() -> bool {
            if (notif_incomplete_timeout_ <= 0 ||
                !_notif_rx_partial_timer_active ||
                _notif_rx_buffer.empty()) {
                return false;
            }

            const auto now = std::chrono::steady_clock::now();
            return now - _notif_rx_partial_started_at >=
                std::chrono::seconds(notif_incomplete_timeout_);
        };

        auto finalize_incomplete_locked = [&](const std::string& reason) {
            if (_notif_rx_buffer.empty()) {
                _notif_rx_partial_timer_active = false;
                return;
            }

            std::string partial = std::move(_notif_rx_buffer);
            _notif_rx_buffer.clear();
            _notif_rx_partial_timer_active = false;

            const std::int64_t partial_bytes =
                static_cast<std::int64_t>(partial.size());

            add_diagnostic_event_locked(
                "incomplete_notification",
                reason,
                partial_bytes,
                true
            );

            enqueue_or_drop_locked(std::move(partial), partial_bytes);
        };

        auto read_currently_available = [&]() -> std::string {
            std::lock_guard<std::mutex> guard(notif_mutex_);

            if (!notif_channel_ || !notif_session_) {
                throw NetconfException("Notification channel/session is not active");
            }

            if (notif_socket_.get() != fd) {
                throw NetconfException("Notification FD does not match active subscription socket");
            }

            return read_available_notification_bytes(
                notif_channel_.get(),
                notif_session_.get()
            );
        };

        std::string bytes = read_currently_available();

        {
            std::lock_guard<std::mutex> lk(_notif_queue_mtx);

            if (!bytes.empty()) {
                _notif_rx_buffer.append(bytes);
            }

            process_rx_buffer_locked();

            if (partial_size_limit_reached_locked()) {
                finalize_incomplete_locked(
                    "Received notification bytes without NETCONF EOM; size guard fired and queued partial notification"
                );
            }
        }

        flush_events_and_notifications();

        while (true) {
            bool should_wait = false;
            int wait_ms = 0;

            {
                std::lock_guard<std::mutex> lk(_notif_queue_mtx);

                process_rx_buffer_locked();

                if (partial_size_limit_reached_locked()) {
                    finalize_incomplete_locked(
                        "Received notification bytes without NETCONF EOM; size guard fired and queued partial notification"
                    );
                } else if (partial_timeout_reached_locked()) {
                    finalize_incomplete_locked(
                        "Received notification bytes without NETCONF EOM; timeout guard fired and queued partial notification"
                    );
                }

                if (!_notif_rx_buffer.empty() && notif_incomplete_timeout_ > 0) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto deadline =
                        _notif_rx_partial_started_at +
                        std::chrono::seconds(notif_incomplete_timeout_);

                    if (now >= deadline) {
                        finalize_incomplete_locked(
                            "Received notification bytes without NETCONF EOM; timeout guard fired and queued partial notification"
                        );
                    } else {
                        const auto remaining_ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                deadline - now
                            ).count();

                        wait_ms = static_cast<int>(
                            std::min<long long>(
                                std::max<long long>(1, remaining_ms),
                                NOTIFICATION_POLL_SLICE_MS
                            )
                        );
                        should_wait = true;
                    }
                }
            }

            flush_events_and_notifications();

            if (!should_wait) {
                break;
            }

            const int ready = poll_notification_fd(fd, wait_ms);
            if (ready == 0) {
                {
                    std::lock_guard<std::mutex> lk(_notif_queue_mtx);
                    if (partial_timeout_reached_locked()) {
                        finalize_incomplete_locked(
                            "Received notification bytes without NETCONF EOM; timeout guard fired and queued partial notification"
                        );
                    }
                }
                flush_events_and_notifications();
                continue;
            }

            std::string more = read_currently_available();

            {
                std::lock_guard<std::mutex> lk(_notif_queue_mtx);
                if (!more.empty()) {
                    _notif_rx_buffer.append(more);
                }

                process_rx_buffer_locked();

                if (partial_size_limit_reached_locked()) {
                    finalize_incomplete_locked(
                        "Received notification bytes without NETCONF EOM; size guard fired and queued partial notification"
                    );
                }
            }

            flush_events_and_notifications();
        }

    } catch (const std::exception& e) {
        throw NetconfException(
            std::string("Unable to read from channel: ") + e.what()
        );
    }
}

std::string NetconfClient::next_notification(int timeout_ms) {
    try {
        {
            std::lock_guard<std::mutex> guard(notif_mutex_);

            if (!notif_channel_) {
                throw NetconfException("Notification channel not open.");
            }

            if (!notif_session_) {
                throw NetconfException("Notification session not open.");
            }

            if (!notif_is_connected_) {
                throw NetconfException("Notification subscription is not active.");
            }
        }

        std::unique_lock<std::mutex> lk(_notif_queue_mtx);

        if (timeout_ms < 0) {
            throw NetconfException("timeout_ms must be >= 0");
        }

        bool got_data = false;

        if (timeout_ms == 0) {
            got_data = !_notif_queue.empty();
        } else {
            got_data = _notif_queue_cv.wait_for(
                lk,
                std::chrono::milliseconds(timeout_ms),
                [&]{ return !_notif_queue.empty(); }
            );
        }

        if (!got_data) {
            return std::string{};
        }

        std::string xml = std::move(_notif_queue.front());
        _notif_queue.pop_front();

        NotificationHealthEvent recovery_event;
        bool emit_recovery_event = false;

        if (_notif_queue_full_state &&
            (_notif_queue_max_size_ < 0 ||
            _notif_queue.size() < static_cast<std::size_t>(_notif_queue_max_size_))) {
            _notif_queue_full_state = false;

            recovery_event = make_notification_health_event_locked(
                "notification_queue_recovered",
                "Notification queue has free capacity again",
                -1,
                0,
                0
            );
            emit_recovery_event = true;
        }

        lk.unlock();

        if (emit_recovery_event) {
            NotificationEventBus::instance().emit(std::move(recovery_event));
        }

        return xml;

    } catch (const std::exception& e) {
        throw NetconfException("Unable to read from queue: " + std::string(e.what()));
    }
}

std::vector<std::string> NetconfClient::peek_notifications(int max_items) {
    if (max_items < -1) {
        throw NetconfException("max_items must be -1 for all items or >= 0");
    }

    std::lock_guard<std::mutex> lk(_notif_queue_mtx);

    std::size_t limit = _notif_queue.size();

    if (max_items >= 0 &&
        static_cast<std::size_t>(max_items) < limit) {
        limit = static_cast<std::size_t>(max_items);
    }

    std::vector<std::string> snapshot;
    snapshot.reserve(limit);

    auto it = _notif_queue.begin();

    for (std::size_t i = 0; i < limit && it != _notif_queue.end(); ++i, ++it) {
        snapshot.push_back(*it);
    }

    return snapshot;
}

std::size_t NetconfClient::notification_queue_size() {
    std::lock_guard<std::mutex> lk(_notif_queue_mtx);
    return _notif_queue.size();
}

NotificationHealthEvent NetconfClient::make_notification_health_event_locked(
    const std::string& type,
    const std::string& message,
    int fd,
    std::int64_t dropped_delta,
    std::int64_t partial_bytes
) const {
    NotificationHealthEvent event;
    event.valid = true;
    event.type = type;
    event.timestamp = current_notification_event_timestamp_utc();
    event.label = label_;
    event.hostname = hostname_;
    event.port = port_;
    event.fd = fd;
    event.message = message;

    event.queue_size = static_cast<std::int64_t>(_notif_queue.size());
    event.queue_max_size = static_cast<std::int64_t>(_notif_queue_max_size_);
    event.queue_high_watermark = static_cast<std::int64_t>(_notif_queue_high_watermark);
    event.notifications_enqueued = static_cast<std::int64_t>(_notif_enqueued_count);
    event.notifications_dropped_queue_full =
        static_cast<std::int64_t>(_notif_dropped_queue_full_count);
    event.notifications_dropped_delta = dropped_delta;
    event.incomplete_notifications_received =
        static_cast<std::int64_t>(_notif_incomplete_count);
    event.partial_bytes = partial_bytes;

    return event;
}

bool NetconfClient::is_subscription_active() const {
    try {
        std::lock_guard<std::mutex> guard(notif_mutex_);

        if (!notif_is_connected_) return false;
        if (!notif_channel_) return false;
        if (!notif_session_) return false;

        int fd = notif_socket_.get();
        if (fd < 0) return false;

        int flags = fcntl(fd, F_GETFD);
        if (flags < 0 && errno == EBADF) return false;

        return true;

    } catch (const std::exception& e) {
        throw NetconfException("Unable to get subscription status: " + std::string(e.what()));
    }
}

std::string NetconfClient::send_rpc_non_blocking(const std::string& rpc) {
    return send_rpc_non_blocking_func(channel_.get(), session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::get_non_blocking(
    const std::string& filter
) {
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

std::string NetconfClient::get_config_non_blocking(
    const std::string& source,
    const std::string& filter
) {
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

std::string NetconfClient::copy_config_non_blocking(
    const std::string& target,
    const std::string& source
) {
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

std::string NetconfClient::delete_config_non_blocking(
    const std::string& target
) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<delete-config>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</delete-config>)"
        R"(</rpc>)";
    return send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::validate_non_blocking(
    const std::string& source
) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<validate>)"
            R"(<source><)" + source + R"(/></source>)"
          R"(</validate>)"
        R"(</rpc>)";
    return send_rpc_non_blocking_func(channel_.get(),  session_.get(), socket_.get(), rpc, read_timeout_);
}

std::string NetconfClient::edit_config_non_blocking(
    const std::string& target,
    const std::string& config,
    bool do_validate
) {
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
    const std::string& filter
) {
    try {
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

        if (notif_socket_.get() < 0) {
            throw NetconfException("No notifications socket present");
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

        // Read the subscription RPC reply before registering with the reactor.
        std::string reply = send_rpc_non_blocking_func(
            notif_channel_.get(),
            notif_session_.get(),
            notif_socket_.get(),
            rpc,
            read_timeout_
        );

        {
            std::lock_guard<std::mutex> lk(_notif_queue_mtx);
            _notif_rx_buffer.clear();
            _notif_rx_partial_timer_active = false;
        }

        // Now it is safe for the reactor to read real <notification> messages.
        NotificationReactorManager::instance().add(
            notif_socket_.get(),
            shared_from_this()
        );
        {
            std::lock_guard<std::mutex> guard(notif_mutex_);
            notif_is_connected_ = true;
            notif_is_blocking_ = false;
        }

        return reply;

    } catch (const std::exception& e) {
        mark_notification_dead();

        throw NetconfException(
            "Unable to Subscribe to device: " + std::string(e.what())
        );
    }
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

std::string NetconfClient::locked_edit_config_non_blocking(
    const std::string& target,
    const std::string& config,
    bool do_validate
) {
    try {
        lock_non_blocking(target);
        std::string reply = edit_config_non_blocking(target, config, do_validate);
        commit_non_blocking();
        unlock_non_blocking(target);
        return reply;
    } catch (const std::exception& err) {
        // RAII wrappers ensure that session_, channel_, and socket_ are cleaned up automatically.
        try {
            unlock_non_blocking(target);
        } catch (...) {
            // Ignore unlock errors since we are already handling an exception.
        }
        throw NetconfException("Unable to complete operation: " + std::string(err.what()));
    }
}
