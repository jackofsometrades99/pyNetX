#include "netconf_client.hpp"
#include <stdexcept>
#include <iostream>
#include <future>
#include <sstream>
#include <libssh/libssh.h>
#include <tinyxml2.h>

// ----------------------- CLEANUP HELPERS -------------------------
static void cleanup_channel(ssh_channel& channel) {
    if (channel) {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        channel = nullptr;
    }
}

static void cleanup_session(ssh_session& session) {
    if (session) {
        ssh_disconnect(session);
        ssh_free(session);
        session = nullptr;
    }
}

// ----------------------- XML Error Checker -------------------------
static void check_for_rpc_error(const std::string& xml_reply) {
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError error = doc.Parse(xml_reply.c_str());
    if (error != tinyxml2::XML_SUCCESS) {
        // ignoring parse error
        return;
    }
    tinyxml2::XMLElement* rpcReply = doc.FirstChildElement("rpc-reply");
    if (!rpcReply) return;
    tinyxml2::XMLElement* rpcErr = rpcReply->FirstChildElement("rpc-error");
    if (!rpcErr) return;
    const char* errMsg = nullptr;
    auto* errElem = rpcErr->FirstChildElement("error-message");
    if (errElem) {
        errMsg = errElem->GetText();
    }
    if (!errMsg) {
        errMsg = "RPC error (unknown error-message)";
    }
    throw NetconfException(std::string("RPC error: ") + errMsg);
}

// ----------------------- NetconfClient Implementation -------------------------
NetconfClient::NetconfClient(const std::string& hostname, int port,
                             const std::string& username, const std::string& password,
                             const std::string& key_path)
    : hostname_(hostname), port_(port),
      username_(username), password_(password), key_path_(key_path),
      session_(nullptr), channel_(nullptr)
{
}

NetconfClient::~NetconfClient() {
    try {
        disconnect();
    } catch(...) {
        // Suppress exceptions in destructor
    }
}

bool NetconfClient::connect() {
    session_ = ssh_new();
    if (!session_) {
        throw NetconfException("Failed to create SSH session (out of memory?)");
    }

    int verbosity = SSH_LOG_PROTOCOL;
    ssh_options_set(session_, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    int strict_hostkey_check = 0;
    ssh_options_set(session_, SSH_OPTIONS_STRICTHOSTKEYCHECK, &strict_hostkey_check);

    ssh_options_set(session_, SSH_OPTIONS_HOST, hostname_.c_str());
    ssh_options_set(session_, SSH_OPTIONS_PORT, &port_);
    ssh_options_set(session_, SSH_OPTIONS_USER, username_.c_str());

    // 1. Connect
    int rc = ssh_connect(session_);
    if (rc != SSH_OK) {
        std::string err = ssh_get_error(session_);
        cleanup_session(session_);
        throw NetconfConnectionRefused("Unable to connect [" + hostname_ + "]: " + err);
    }

    // 2. Authenticate (password)
    rc = ssh_userauth_password(session_, nullptr, password_.c_str());
    if (rc != SSH_AUTH_SUCCESS) {
        std::string err = ssh_get_error(session_);
        cleanup_session(session_);
        throw NetconfAuthError("Authentication failed [" + hostname_ + "]: " + err);
    }

    // 3. Create channel
    channel_ = ssh_channel_new(session_);
    if (!channel_) {
        cleanup_session(session_);
        throw NetconfChannelError("Failed to create channel for NETCONF");
    }
    if (ssh_channel_open_session(channel_) != SSH_OK) {
        std::string err = ssh_get_error(session_);
        cleanup_channel(channel_);
        cleanup_session(session_);
        throw NetconfChannelError("Failed to open channel: " + err);
    }
    if (ssh_channel_request_subsystem(channel_, "netconf") != SSH_OK) {
        std::string err = ssh_get_error(session_);
        cleanup_channel(channel_);
        cleanup_session(session_);
        throw NetconfChannelError("Failed to request NETCONF subsystem: " + err);
    }

    // 4. Read server <hello>
    std::string server_hello = read_until_eom();
    // (Optional: parse server capabilities from server_hello)

    // 5. Send client <hello>
    send_client_hello();
    return true;
}

void NetconfClient::disconnect() {
    if (channel_) {
        cleanup_channel(channel_);
    }
    if (session_) {
        cleanup_session(session_);
    }
}

std::string NetconfClient::read_until_eom() {
    std::string response;
    char buffer[512];
    while (true) {
        int nbytes = ssh_channel_read(channel_, buffer, sizeof(buffer), 0);
        if (nbytes < 0) {
            throw NetconfException("Error reading from channel: " +
                                   std::string(ssh_get_error(session_)));
        }
        if (nbytes == 0) {
            // EOF or channel closed
            break;
        }
        response.append(buffer, nbytes);
        size_t pos = response.find("]]>]]>");
        if (pos != std::string::npos) {
            break;
        }
    }
    return response;
}

std::string NetconfClient::build_client_hello() {
    return
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<hello xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">)"
          R"(<capabilities>)"
            R"(<capability>urn:ietf:params:netconf:base:1.0</capability>)"
          R"(</capabilities>)"
        R"(</hello>)"
        "]]>]]>";
}

void NetconfClient::send_client_hello() {
    std::string hello = build_client_hello();
    if (ssh_channel_write(channel_, hello.c_str(), hello.size()) < 0) {
        throw NetconfException("Failed to send client <hello>: " +
                               std::string(ssh_get_error(session_)));
    }
}

std::string NetconfClient::send_rpc(const std::string& rpc) {
    if (!channel_) {
        throw NetconfException("Channel not open.");
    }
    std::string rpc_with_eom = rpc + "\n]]>]]>\n";
    if (ssh_channel_write(channel_, rpc_with_eom.c_str(), rpc_with_eom.size()) < 0) {
        throw NetconfException("Failed to send RPC: " +
                               std::string(ssh_get_error(session_)));
    }
    std::string reply = read_until_eom();
    check_for_rpc_error(reply);
    return reply;
}

std::string NetconfClient::receive_notification() {
    if (!channel_) {
        throw NetconfException("Channel not open.");
    }
    return read_until_eom();
}

std::string NetconfClient::get(const std::string& filter) {
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<get>)";
    if (!filter.empty()) {
        rpc += R"(<filter type="subtree">)" + filter + "</filter>";
    }
    rpc += R"(</get></rpc>)";
    return send_rpc(rpc);
}

std::string NetconfClient::get_config(const std::string& source,
                                      const std::string& filter)
{
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<get-config>)"
            R"(<source><)" + source + R"(/></source>)";
    if (!filter.empty()) {
        rpc += R"(<filter type="subtree">)" + filter + "</filter>";
    }
    rpc += R"(</get-config></rpc>)";
    return send_rpc(rpc);
}

std::string NetconfClient::copy_config(const std::string& target,
                                       const std::string& source)
{
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<copy-config>)"
            R"(<target><)" + target + R"(/></target>)"
            R"(<source><)" + source + R"(/></source>)"
          R"(</copy-config>)"
        R"(</rpc>)";
    return send_rpc(rpc);
}

std::string NetconfClient::delete_config(const std::string& target)
{
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<delete-config>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</delete-config>)"
        R"(</rpc>)";
    return send_rpc(rpc);
}

std::string NetconfClient::validate(const std::string& source)
{
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<validate>)"
            R"(<source><)" + source + R"(/></source>)"
          R"(</validate>)"
        R"(</rpc>)";
    return send_rpc(rpc);
}

std::string NetconfClient::edit_config(const std::string& target,
                                       const std::string& config,
                                       bool do_validate)
{
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<edit-config>)"
            R"(<target><)" + target + R"(/></target>)"
            R"(<config>)" + config + R"(</config>)"
          R"(</edit-config>)"
        R"(</rpc>)";

    std::string reply = send_rpc(rpc);
    if (do_validate) {
        validate(target);
    }
    return reply;
}

std::string NetconfClient::subscribe(const std::string& stream,
                                     const std::string& filter)
{
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<create-subscription xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0">)"
            R"(<stream>)" + stream + R"(</stream>)";
    if (!filter.empty()) {
        rpc += R"(<filter type="subtree">)" + filter + "</filter>";
    }
    rpc += R"(</create-subscription></rpc>)";
    return send_rpc(rpc);
}

std::string NetconfClient::lock(const std::string& target)
{
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<lock>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</lock>)"
        R"(</rpc>)";
    return send_rpc(rpc);
}

std::string NetconfClient::unlock(const std::string& target)
{
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<unlock>)"
            R"(<target><)" + target + R"(/></target>)"
          R"(</unlock>)"
        R"(</rpc>)";
    return send_rpc(rpc);
}

std::string NetconfClient::commit()
{
    std::string rpc =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="101">)"
          R"(<commit/>)"
        R"(</rpc>)";
    return send_rpc(rpc);
}

std::string NetconfClient::locked_edit_config(const std::string& target,
                                              const std::string& config,
                                              bool do_validate)
{
    lock(target);
    std::string reply = edit_config(target, config, do_validate);
    commit();
    unlock(target);
    return reply;
}

// ----------------------- Asynchronous Methods -------------------------
std::future<bool> NetconfClient::connect_async() {
    auto self = shared_from_this();
    return std::async(std::launch::async, [self]() {
        return self->connect();
    });
}

std::future<void> NetconfClient::disconnect_async() {
    auto self = shared_from_this();
    return std::async(std::launch::async, [self]() {
        self->disconnect();
    });
}

std::future<std::string> NetconfClient::send_rpc_async(const std::string& rpc) {
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, rpc]() {
        return self->send_rpc(rpc);
    });
}

std::future<std::string> NetconfClient::receive_notification_async() {
    auto self = shared_from_this();
    return std::async(std::launch::async, [self]() {
        return self->receive_notification();
    });
}

std::future<std::string> NetconfClient::get_async(const std::string& filter) {
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, filter]() {
        return self->get(filter);
    });
}

std::future<std::string> NetconfClient::get_config_async(const std::string& source,
                                                         const std::string& filter)
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, source, filter]() {
        return self->get_config(source, filter);
    });
}

std::future<std::string> NetconfClient::copy_config_async(const std::string& target,
                                                          const std::string& source)
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, target, source]() {
        return self->copy_config(target, source);
    });
}

std::future<std::string> NetconfClient::delete_config_async(const std::string& target)
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, target]() {
        return self->delete_config(target);
    });
}

std::future<std::string> NetconfClient::validate_async(const std::string& source)
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, source]() {
        return self->validate(source);
    });
}

std::future<std::string> NetconfClient::edit_config_async(const std::string& target,
                                                          const std::string& config,
                                                          bool do_validate)
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, target, config, do_validate]() {
        return self->edit_config(target, config, do_validate);
    });
}

std::future<std::string> NetconfClient::subscribe_async(const std::string& stream,
                                                        const std::string& filter)
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, stream, filter]() {
        return self->subscribe(stream, filter);
    });
}

std::future<std::string> NetconfClient::lock_async(const std::string& target)
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, target]() {
        return self->lock(target);
    });
}

std::future<std::string> NetconfClient::unlock_async(const std::string& target)
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, target]() {
        return self->unlock(target);
    });
}

std::future<std::string> NetconfClient::commit_async()
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self]() {
        return self->commit();
    });
}

std::future<std::string> NetconfClient::locked_edit_config_async(const std::string& target,
                                                                 const std::string& config,
                                                                 bool do_validate)
{
    auto self = shared_from_this();
    return std::async(std::launch::async, [self, target, config, do_validate]() {
        return self->locked_edit_config(target, config, do_validate);
    });
}
