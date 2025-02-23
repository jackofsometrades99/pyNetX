#include "netconf_client.hpp"
#include <stdexcept>
#include <iostream>
#include <libssh2.h>

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
NetconfClient::NetconfClient(const std::string& hostname, int port,
                             const std::string& username, const std::string& password,
                             const std::string& key_path, int connect_timeout, int read_timeout)
    : hostname_(hostname), port_(port),
      username_(username), password_(password), key_path_(key_path),
      session_(nullptr), channel_(nullptr), notif_session_(nullptr),
      notif_channel_(nullptr), connect_timeout_(connect_timeout),
      read_timeout_(read_timeout)
{
}

NetconfClient::~NetconfClient() {
    try {
        disconnect();
    } catch(...) {
        // Suppress exceptions in destructor.
    }
}
