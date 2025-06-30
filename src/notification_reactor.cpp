#include "notification_reactor.hpp"
#include "netconf_client.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdexcept>

NotificationReactor& NotificationReactor::instance() {
    static NotificationReactor inst;
    return inst;
}

NotificationReactor::NotificationReactor()
  : _running(true)
{
    _epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (_epoll_fd < 0) {
        throw std::runtime_error("NotificationReactor: epoll_create1 failed");
    }
    _reactor_thread = std::thread(&NotificationReactor::loop, this);
}

NotificationReactor::~NotificationReactor() {
    _running = false;
    if (_reactor_thread.joinable()) {
        _reactor_thread.join();
    }
    ::close(_epoll_fd);
}

void NotificationReactor::add(int fd, NetconfClient* client) {
    std::lock_guard<std::mutex> guard(_mtx);

    struct epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLERR | EPOLLRDHUP;
    ev.data.fd = fd;

    if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        throw std::runtime_error("NotificationReactor: epoll_ctl ADD failed");
    }
    _handlers[fd] = client;
}

void NotificationReactor::remove(int fd) {
    std::lock_guard<std::mutex> guard(_mtx);
    epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    _handlers.erase(fd);
}

void NotificationReactor::loop() {
  while (_running) {
    struct epoll_event events[64];
    int n = epoll_wait(_epoll_fd, events, 64, -1);
    if (n < 0) {
      if (errno == EINTR) {
        continue;            // signal, just retry
      }
      if (errno == EBADF) {
        // our epoll FD is invalid — try to rebuild
        std::lock_guard<std::mutex> guard(_mtx);
        ::close(_epoll_fd);
        _epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (_epoll_fd < 0) {
          throw std::runtime_error("Recreating epoll failed");
        }
        // re-add every fd back into epoll
        for (auto& [fd, client] : _handlers) {
          struct epoll_event ev{ EPOLLIN | EPOLLERR | EPOLLRDHUP, { .fd = fd } };
          if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            // if a given socket is closed/invalid, remove from handlers
            if (errno == ENOENT || errno == EBADF) {
              _handlers.erase(fd);
            } else {
              throw std::runtime_error("Failed to re-add FD to epoll");
            }
          }
        }
        continue;
      }
      // some other error
      throw std::runtime_error(std::string("epoll_wait error: ") + strerror(errno));
    }
    std::lock_guard<std::mutex> guard(_mtx);
    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      auto it = _handlers.find(fd);
      if (it != _handlers.end()) {
        it->second->on_notification_ready(fd);
      }
    }
  }
}
