#include "notification_reactor.hpp"
#include "netconf_client.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdexcept>
#include <iostream>

NotificationReactor& NotificationReactor::instance() {
    static NotificationReactor inst;
    return inst;
}

NotificationReactor::NotificationReactor()
  : _running(true)
{
    try {
      _epoll_fd = epoll_create1(EPOLL_CLOEXEC);
      if (_epoll_fd < 0) {
          throw NetconfException("NotificationReactor: epoll_create1 failed");
      }
      _reactor_thread = std::thread(&NotificationReactor::loop, this);
    }  catch (const std::exception& e) {
        throw NetconfException(std::string("Error happened while assigning to notification reactor: " + std::string(e.what())));
    }
}

NotificationReactor::~NotificationReactor() {
  try {
    _running = false;
    if (_reactor_thread.joinable()) {
        _reactor_thread.join();
    }
    ::close(_epoll_fd);
    } catch (const std::exception& e) {
        std::cerr << "Error happened while closing reactor pool: " << std::string(e.what()) << '\n';
    } catch (...) {
        std::cerr << "Unknown error while closing reactor pool";
    }
}

void NotificationReactor::add(int fd, NetconfClient* client) {
  try {
    std::lock_guard<std::mutex> guard(_mtx);

    struct epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLERR | EPOLLRDHUP;
    ev.data.fd = fd;

    if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        throw NetconfException("NotificationReactor: epoll_ctl ADD failed");
    }
    _handlers[fd] = client;
  }  catch (const std::exception& e) {
      throw NetconfException(std::string("Error happened while adding to reactor pool: " + std::string(e.what())));
  }
}

void NotificationReactor::remove(int fd) {
  try {
    std::lock_guard<std::mutex> guard(_mtx);
    epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    _handlers.erase(fd);
  }  catch (const std::exception& e) {
      throw NetconfException(std::string("Error happened while removing from reactor pool: " + std::string(e.what())));
  }
}

void NotificationReactor::loop() {
  while (_running) {
    try {
      struct epoll_event events[64];
      int n = epoll_wait(_epoll_fd, events, 64, -1);
      if (n < 0) {
        if (errno == EINTR) {
          continue;            // signal, just retry
        }
        if (errno == EBADF) {
            std::lock_guard<std::mutex> guard(_mtx);

            ::close(_epoll_fd);
            _epoll_fd = epoll_create1(EPOLL_CLOEXEC);
            if (_epoll_fd < 0) {
                throw NetconfException("Recreating epoll failed");
            }

            for (auto it = _handlers.begin(); it != _handlers.end(); ) {
                int fd = it->first;

                struct epoll_event ev = {};
                ev.events = EPOLLIN | EPOLLERR | EPOLLRDHUP;
                ev.data.fd = fd;

                if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
                    if (errno == ENOENT || errno == EBADF) {
                        it = _handlers.erase(it);
                        continue;
                    }
                    throw NetconfException("Failed to re-add FD to epoll");
                }

                ++it;
            }

            continue;
        }
        // some other error
        throw NetconfException(std::string("epoll_wait error: ") + strerror(errno));
      }
      std::lock_guard<std::mutex> guard(_mtx);
      for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        auto it = _handlers.find(fd);
        if (it != _handlers.end()) {
          try {
          it->second->on_notification_ready(fd);
          } catch (const std::exception& e) {
            throw NetconfException(std::string("Error assigning FD's to loop: " + std::string(e.what())));
          }
        }
      }
    }   catch (const std::exception& e) {
        throw NetconfException(std::string("Error happened while reading notification in loop from FD: " + std::string(e.what())));
    }
  }
}
