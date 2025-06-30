#ifndef NOTIFICATION_REACTOR_HPP
#define NOTIFICATION_REACTOR_HPP

#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>

class NetconfClient;

class NotificationReactor {
public:
    static NotificationReactor& instance();

    // lifecycle
    NotificationReactor(const NotificationReactor&) = delete;
    NotificationReactor& operator=(const NotificationReactor&) = delete;

    // **make these public**
    NotificationReactor();
    ~NotificationReactor();

    // register/unregister
    void add(int fd, NetconfClient* client);
    void remove(int fd);

private:
    void loop();
    // reactor loop state
    int _epoll_fd;
    std::thread _reactor_thread;
    std::atomic<bool> _running{false};
    std::mutex _mtx;
    std::unordered_map<int,NetconfClient*> _handlers;
};

#endif // NOTIFICATION_REACTOR_HPP