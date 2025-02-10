#ifndef NETCONF_CLIENT_HPP
#define NETCONF_CLIENT_HPP

#include <string>
#include <future>
#include <stdexcept>
#include <memory>
#include <libssh/libssh.h>
#include <tinyxml2.h>

/** Base exception for any Netconf-related errors. */
class NetconfException : public std::runtime_error {
public:
    explicit NetconfException(const std::string& msg)
        : std::runtime_error(msg) {}
};

/** More specific exceptions for different failure modes. */
class NetconfConnectionRefused : public NetconfException {
public:
    using NetconfException::NetconfException;
};

class NetconfAuthError : public NetconfException {
public:
    using NetconfException::NetconfException;
};

class NetconfChannelError : public NetconfException {
public:
    using NetconfException::NetconfException;
};

class NetconfClient : public std::enable_shared_from_this<NetconfClient>
{
public:
    NetconfClient(const std::string& hostname, int port,
                  const std::string& username, const std::string& password,
                  const std::string& key_path = "");
    ~NetconfClient();

    // ----------------------- Synchronous Methods -------------------------
    bool connect();
    void disconnect();
    std::string send_rpc(const std::string& rpc);
    std::string get(const std::string& filter = "");
    std::string get_config(const std::string& source = "running",
                           const std::string& filter = "");
    std::string edit_config(const std::string& target,
                            const std::string& config,
                            bool do_validate = false);
    std::string subscribe(const std::string& stream = "NETCONF",
                          const std::string& filter = "");
    std::string copy_config(const std::string& target,
                            const std::string& source);
    std::string delete_config(const std::string& target);
    std::string validate(const std::string& source = "running");
    std::string lock(const std::string& target = "running");
    std::string unlock(const std::string& target = "running");
    std::string commit();
    std::string locked_edit_config(const std::string& target,
                                   const std::string& config,
                                   bool do_validate=false);
    std::string receive_notification();

    // ----------------------- Asynchronous Methods -------------------------
    std::future<bool> connect_async();
    std::future<void> disconnect_async();
    std::future<std::string> send_rpc_async(const std::string& rpc);
    std::future<std::string> get_async(const std::string& filter = "");
    std::future<std::string> get_config_async(const std::string& source="running",
                                              const std::string& filter="");
    std::future<std::string> edit_config_async(const std::string& target,
                                               const std::string& config,
                                               bool do_validate=false);
    std::future<std::string> subscribe_async(const std::string& stream="NETCONF",
                                             const std::string& filter="");
    std::future<std::string> copy_config_async(const std::string& target,
                                               const std::string& source);
    std::future<std::string> delete_config_async(const std::string& target);
    std::future<std::string> validate_async(const std::string& source="running");
    std::future<std::string> lock_async(const std::string& target="running");
    std::future<std::string> unlock_async(const std::string& target="running");
    std::future<std::string> commit_async();
    std::future<std::string> locked_edit_config_async(const std::string& target,
                                                      const std::string& config,
                                                      bool do_validate=false);
    std::future<std::string> receive_notification_async();

private:
    std::string read_until_eom();
    std::string build_client_hello();
    void send_client_hello();

private:
    std::string hostname_;
    int port_;
    std::string username_;
    std::string password_;
    std::string key_path_;

    ssh_session session_;
    ssh_channel channel_;
};

#endif // NETCONF_CLIENT_HPP
