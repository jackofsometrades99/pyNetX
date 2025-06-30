// NETCONF_CLIENT_HPP

#ifndef NETCONF_CLIENT_HPP
#define NETCONF_CLIENT_HPP
#include "notification_reactor.hpp"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <string>
#include <future>
#include <stdexcept>
#include <memory>
#include <libssh2.h>
#include <tinyxml2.h>
#include <atomic>
#include <unistd.h>
#include <sys/epoll.h>

// RAII Wrapper for an epoll file descriptor.
class EpollRAII {
public:
    explicit EpollRAII(int fd = -1) : fd_(fd) {}
    ~EpollRAII() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    EpollRAII(const EpollRAII&) = delete;
    EpollRAII& operator=(const EpollRAII&) = delete;
    EpollRAII(EpollRAII&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    EpollRAII& operator=(EpollRAII&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    int get() const { return fd_; }
    void reset(int fd = -1) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }
private:
    int fd_;
};


// If LIBSSH2_DISCONNECT_NORMAL is not defined, define it to 0.
#ifndef LIBSSH2_DISCONNECT_NORMAL
#define LIBSSH2_DISCONNECT_NORMAL 0
#endif

// If libssh2_channel_request_subsystem is missing, define a fallback using process_startup.
#ifndef libssh2_channel_request_subsystem
#define libssh2_channel_request_subsystem(channel, subsystem) \
    libssh2_channel_process_startup(channel, "subsystem", 9, subsystem, strlen(subsystem))
#endif

// If libssh2_channel_read_nonblocking is not available, fallback to libssh2_channel_read.
// Note: This assumes that the session/channel is in nonblocking mode.
#ifndef libssh2_channel_read_nonblocking
#define libssh2_channel_read_nonblocking(channel, buf, buflen, streamid) \
    libssh2_channel_read(channel, buf, buflen)
#endif


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

//
// RAII Wrapper for socket file descriptor.
//
class SocketRAII {
    public:
        explicit SocketRAII(int fd = -1) : fd_(fd) {}
        ~SocketRAII() {
            if (fd_ >= 0) {
                ::close(fd_);
            }
        }
        SocketRAII(const SocketRAII&) = delete;
        SocketRAII& operator=(const SocketRAII&) = delete;
        SocketRAII(SocketRAII&& other) noexcept : fd_(other.fd_) {
            other.fd_ = -1;
        }
        SocketRAII& operator=(SocketRAII&& other) noexcept {
            if (this != &other) {
                if (fd_ >= 0) { ::close(fd_); }
                fd_ = other.fd_;
                other.fd_ = -1;
            }
            return *this;
        }
        int get() const { return fd_; }
        void reset(int fd = -1) {
            if (fd_ >= 0) { ::close(fd_); }
            fd_ = fd;
        }
    private:
        int fd_;
    };
    
    //
    // Custom deleter for LIBSSH2_SESSION, to be used with std::unique_ptr.
    //
    struct Libssh2SessionDeleter {
        void operator()(LIBSSH2_SESSION* session) const {
            if (session) {
                libssh2_session_disconnect_ex(session, LIBSSH2_DISCONNECT_NORMAL, "Normal Shutdown", "");
                libssh2_session_free(session);
            }
        }
    };
    using SessionPtr = std::unique_ptr<LIBSSH2_SESSION, Libssh2SessionDeleter>;
    
    //
    // Custom deleter for LIBSSH2_CHANNEL, to be used with std::unique_ptr.
    //
    struct Libssh2ChannelDeleter {
        void operator()(LIBSSH2_CHANNEL* channel) const {
            if (channel) {
                libssh2_channel_close(channel);
                libssh2_channel_free(channel);
            }
        }
    };
    using ChannelPtr = std::unique_ptr<LIBSSH2_CHANNEL, Libssh2ChannelDeleter>;
    
//
// NetconfClient class using RAII wrappers.
//
class NetconfClient : public std::enable_shared_from_this<NetconfClient>
{
public:
    NetconfClient(const std::string& hostname, int port,
                  const std::string& username, const std::string& password,
                  const std::string& key_path = "", int connect_timeout = 60,
                  int read_timeout = 60);
    ~NetconfClient();

    // ----------------------- Blocking Methods -------------------------
    bool connect_blocking();
    bool connect_notification_blocking();
    std::string send_rpc_blocking(const std::string& rpc);
    std::string get_blocking(const std::string& filter = "");
    std::string get_config_blocking(const std::string& source = "running",
                           const std::string& filter = "");
    std::string edit_config_blocking(const std::string& target,
                            const std::string& config,
                            bool do_validate = false);
    std::string subscribe_blocking(const std::string& stream = "NETCONF",
                          const std::string& filter = "");
    std::string copy_config_blocking(const std::string& target,
                            const std::string& source);
    std::string delete_config_blocking(const std::string& target);
    std::string validate_blocking(const std::string& source = "running");
    std::string lock_blocking(const std::string& target = "running");
    std::string unlock_blocking(const std::string& target = "running");
    std::string commit_blocking();
    std::string locked_edit_config_blocking(const std::string& target,
                                   const std::string& config,
                                   bool do_validate=false);
    std::string receive_notification_blocking();

    // ----------------------- Non Blocking Methods -------------------------
    bool connect_non_blocking();
    bool connect_notification_non_blocking();
    std::string send_rpc_non_blocking(const std::string& rpc);
    std::string get_non_blocking(const std::string& filter = "");
    std::string get_config_non_blocking(const std::string& source = "running",
                           const std::string& filter = "");
    std::string edit_config_non_blocking(const std::string& target,
                            const std::string& config,
                            bool do_validate = false);
    std::string subscribe_non_blocking(const std::string& stream = "NETCONF",
                          const std::string& filter = "");
    std::string copy_config_non_blocking(const std::string& target,
                            const std::string& source);
    std::string delete_config_non_blocking(const std::string& target);
    std::string validate_non_blocking(const std::string& source = "running");
    std::string lock_non_blocking(const std::string& target = "running");
    std::string unlock_non_blocking(const std::string& target = "running");
    std::string commit_non_blocking();
    std::string locked_edit_config_non_blocking(const std::string& target,
                                   const std::string& config,
                                   bool do_validate=false);
    std::string next_notification();
    void on_notification_ready(int fd);

    // ----------------------- Synchronous Wrappers -------------------------

    bool connect_sync();
    void disconnect_sync();
    std::string send_rpc_sync(const std::string& rpc);
    std::string get_sync(const std::string& filter = "");
    std::string get_config_sync(const std::string& source = "running",
                           const std::string& filter = "");
    std::string edit_config_sync(const std::string& target,
                            const std::string& config,
                            bool do_validate = false);
    std::string subscribe_sync(const std::string& stream = "NETCONF",
                          const std::string& filter = "");
    std::string copy_config_sync(const std::string& target,
                            const std::string& source);
    std::string delete_config_sync(const std::string& target);
    std::string validate_sync(const std::string& source = "running");
    std::string lock_sync(const std::string& target = "running");
    std::string unlock_sync(const std::string& target = "running");
    std::string commit_sync();
    std::string locked_edit_config_sync(const std::string& target,
                                   const std::string& config,
                                   bool do_validate=false);
    std::string receive_notification_sync();

    // ----------------------- Asynchronous Wrappers -------------------------
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
    
    // Disconnect method (common to all modes)
    bool is_subscription_active() const;
    void disconnect();
    void delete_notification_session();
    void delete_subsription();

private:
    static std::string read_until_eom_blocking(
        LIBSSH2_CHANNEL *chan,
        LIBSSH2_SESSION *sess,
        int read_timeout
    );
    static std::string read_until_eom_non_blocking(
        LIBSSH2_CHANNEL *chan,
        LIBSSH2_SESSION *sess,
        int read_timeout
    );
    static std::string build_client_hello();
    static void send_client_hello_blocking(
        LIBSSH2_CHANNEL *chan,
        LIBSSH2_SESSION *sess
    );
    static void send_client_hello_non_blocking(
        LIBSSH2_CHANNEL *chan,
        LIBSSH2_SESSION *sess,
        int soc_fd
    );
    static std::string send_rpc_blocking_func(
        LIBSSH2_CHANNEL *chan,
        LIBSSH2_SESSION *sess,
        const std::string& rpc,
        int read_timeout
    );
    static std::string send_rpc_non_blocking_func(
        LIBSSH2_CHANNEL *chan,
        LIBSSH2_SESSION *sess,
        int soc_fd,
        const std::string& rpc,
        int read_timeout
    );
    static void check_for_rpc_error(const std::string &xml_reply);
    static std::string resolve_hostname_blocking(const std::string &hostname);
    static std::string resolve_hostname_non_blocking(const std::string &hostname, int timeout_seconds);

    private:
    std::string hostname_;
    int port_;
    std::string username_;
    std::string password_;
    std::string key_path_;
    int connect_timeout_;
    int read_timeout_;
    std::string resolved_host_;

    std::mutex session_mutex_;
    std::mutex ssh_mutex_;
    std::mutex dns_mutex_;
    EpollRAII epoll_fd_;            // Managed epoll descriptor
    bool is_connected_       = false;
    bool is_blocking_        = false;
    bool notif_is_connected_ = false;
    bool notif_is_blocking_  = false;

    std::mutex _notif_queue_mtx;
    std::condition_variable  _notif_queue_cv;
    std::deque<std::string>  _notif_queue;

    // RAII-managed resources:
    SessionPtr session_;      // libssh2 session.
    ChannelPtr channel_;      // libssh2 channel.
    SocketRAII socket_;       // Socket file descriptor for RPC session.

    // SECONDARY session/channel for notifications
    SessionPtr notif_session_;
    ChannelPtr notif_channel_;
    SocketRAII notif_socket_; // Notification session socket
};


#endif // NETCONF_CLIENT_HPP
