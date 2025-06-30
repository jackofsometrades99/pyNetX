// notification_reactor_manager.hpp
#pragma once

#include "notification_reactor.hpp"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <thread>

class NotificationReactorManager {
public:
  static NotificationReactorManager& instance() {
    static NotificationReactorManager M;
    return M;
  }

  /// Initialize with how many devices you expect to monitor.
  /// It will pick a reasonable thread count automatically.
  void init(size_t total_devices);

  /// Change reactor thread count on the fly.
  void set_reactor_count(size_t new_count);

  /// Register a new notification FD → client
  void add(int fd, NetconfClient* client);

  /// Unregister an FD
  void remove(int fd);

private:
  NotificationReactorManager() = default;

  std::vector<std::unique_ptr<NotificationReactor>> reactors_;
  std::vector<size_t> device_counts_;
  std::unordered_map<int,size_t> fd_to_reactor_;
  std::unordered_map<int,NetconfClient*> fd_to_client_;
  std::mutex mtx_;
};
