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
    int notif_queue_size, int socket_connect_timeout,
    int notif_incomplete_max_kb, int notif_incomplete_timeout,
    int notif_drop_event_threshold, const std::string& label
)
    : hostname_(hostname), port_(port),
      username_(username), password_(password), key_path_(key_path),
      label_(label), session_(nullptr), channel_(nullptr),
      notif_session_(nullptr), notif_channel_(nullptr), connect_timeout_(connect_timeout),
      read_timeout_(read_timeout), _notif_queue_max_size_(notif_queue_size),
      socket_connect_timeout_(socket_connect_timeout),
      notif_incomplete_max_kb_(notif_incomplete_max_kb),
      notif_incomplete_timeout_(notif_incomplete_timeout),
      notif_drop_event_threshold_(notif_drop_event_threshold)
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
    if (notif_incomplete_max_kb_ < -1 || notif_incomplete_max_kb_ == 0) {
        throw std::invalid_argument(
            "notif_incomplete_max_kb must be -1 to disable or greater than 0"
        );
    }

    if (notif_incomplete_timeout_ < -1 || notif_incomplete_timeout_ == 0) {
        throw std::invalid_argument(
            "notif_incomplete_timeout must be -1 to disable or greater than 0"
        );
    }
    if (notif_incomplete_max_kb_ < 0 && notif_incomplete_timeout_ < 0) {
        throw std::invalid_argument(
            "At least one of notif_incomplete_max_kb or notif_incomplete_timeout must be enabled"
        );
    }
    if (notif_drop_event_threshold_ <= 0) {
        throw std::invalid_argument(
            "notif_drop_event_threshold must be greater than 0"
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
