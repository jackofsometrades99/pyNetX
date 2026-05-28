#include "notification_reactor_manager.hpp"
#include "notification_reactor.hpp"
#include "netconf_client.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdexcept>
#include <iostream>
#include <memory>

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

void NotificationReactor::add(int fd, std::weak_ptr<NetconfClient> client) {
    try {
        std::lock_guard<std::mutex> guard(_mtx);

        if (fd < 0) {
            throw NetconfException("NotificationReactor: invalid FD");
        }

        if (client.expired()) {
            throw NetconfException("NotificationReactor: expired client");
        }

        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLERR | EPOLLRDHUP;
        ev.data.fd = fd;

        if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            throw NetconfException(
                "NotificationReactor: epoll_ctl ADD failed: " +
                std::string(strerror(errno))
            );
        }

        _handlers[fd] = std::move(client);

    } catch (const std::exception& e) {
        throw NetconfException(
            "Error happened while adding to reactor pool: " +
            std::string(e.what())
        );
    }
}

void NotificationReactor::remove(int fd) {
    std::lock_guard<std::mutex> guard(_mtx);

    if (fd < 0) {
        return;
    }

    epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    _handlers.erase(fd);
}

void NotificationReactor::loop() {
    while (_running) {
        struct epoll_event events[64];

        int n = epoll_wait(_epoll_fd, events, 64, 1000);

        if (!_running) {
            break;
        }

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "NotificationReactor: epoll_wait failed: "
                      << strerror(errno) << std::endl;
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            std::shared_ptr<NetconfClient> client;

            {
                std::lock_guard<std::mutex> guard(_mtx);

                auto it = _handlers.find(fd);
                if (it == _handlers.end()) {
                    continue;
                }

                client = it->second.lock();

                if (!client) {
                    epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    _handlers.erase(it);
                    continue;
                }
            }

            auto cleanup_dead_fd = [&]() noexcept {
                try {
                    NotificationReactorManager::instance().remove(fd);
                } catch (const std::exception& e) {
                    std::cerr << "NotificationReactor: manager remove failed for FD "
                              << fd << ": " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "NotificationReactor: manager remove failed for FD "
                              << fd << ": unknown error" << std::endl;
                }

                try {
                    remove(fd);
                } catch (...) {
                    // remove() should not throw, but stay defensive.
                }

                try {
                    if (client) {
                        client->mark_notification_dead();
                    }
                } catch (...) {
                    // mark_notification_dead() is noexcept, but stay defensive.
                }
            };

            if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                std::cerr << "NotificationReactor: notification FD "
                          << fd << " closed or errored; cleaning up"
                          << std::endl;

                cleanup_dead_fd();
                continue;
            }

            try {
                client->on_notification_ready(fd);
            } catch (const std::exception& e) {
                std::cerr << "NotificationReactor: notification read failed on FD "
                          << fd << ": " << e.what()
                          << "; cleaning up"
                          << std::endl;

                cleanup_dead_fd();
                continue;
            } catch (...) {
                std::cerr << "NotificationReactor: unknown notification read failure on FD "
                          << fd << "; cleaning up"
                          << std::endl;

                cleanup_dead_fd();
                continue;
            }
        }
    }
}
