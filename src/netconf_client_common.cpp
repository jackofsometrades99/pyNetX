#include "netconf_client.hpp"
#include <stdexcept>
#include <iostream>
#include <libssh2.h>
#include <stdexcept>

// ----------------------- Fallback macros -------------------------
#ifndef LIBSSH2_DISCONNECT_NORMAL
#define LIBSSH2_DISCONNECT_NORMAL 0
#endif

#ifndef libssh2_channel_request_subsystem
#define libssh2_channel_request_subsystem(channel, subsystem) \
    libssh2_channel_process_startup(channel, "subsystem", 9, subsystem, strlen(subsystem))
#endif

#ifndef libssh2_channel_read_nonblocking
#define libssh2_channel_read_nonblocking(channel, buf, buflen, streamid) \
    libssh2_channel_read(channel, buf, buflen)
#endif


// ----------------------- NetconfClient Implementation -------------------------
NetconfClient::NetconfClient(
    const std::string& hostname, int port,
    const std::string& username, const std::string& password,
    const std::string& key_path, int connect_timeout, int read_timeout,
    int notif_queue_size, int socket_connect_timeout
)
    : hostname_(hostname), port_(port),
      username_(username), password_(password), key_path_(key_path),
      session_(nullptr), channel_(nullptr), notif_session_(nullptr),
      notif_channel_(nullptr), connect_timeout_(connect_timeout),
      read_timeout_(read_timeout), _notif_queue_max_size_(notif_queue_size),
      socket_connect_timeout_(socket_connect_timeout)
{
    if (hostname_.empty()) {
        throw std::invalid_argument("hostname cannot be empty");
    }

    if (port_ <= 0 || port_ > 65535) {
        throw std::invalid_argument("port must be between 1 and 65535");
    }

    if (username_.empty()) {
        throw std::invalid_argument("username cannot be empty");
    }

    if (connect_timeout_ <= 0) {
        throw std::invalid_argument("connect_timeout must be greater than 0");
    }

    if (read_timeout_ <= 0) {
        throw std::invalid_argument("read_timeout must be greater than 0");
    }

    if (socket_connect_timeout_ <= 0) {
        throw std::invalid_argument("socket_connect_timeout must be greater than 0");
    }

    if (socket_connect_timeout_ > connect_timeout_) {
        throw std::invalid_argument(
            "socket_connect_timeout cannot be greater than connect_timeout"
        );
    }

    if (_notif_queue_max_size_ < -1) {
        throw std::invalid_argument(
            "notif_queue_size must be -1 for unbounded or >= 0 for bounded queue"
        );
    }
}

NetconfClient::~NetconfClient() {
    try {
        disconnect();
    } catch(...) {
        // Suppress exceptions in destructor.
    }
}
