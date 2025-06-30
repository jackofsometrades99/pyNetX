// notification_reactor_manager.cpp
#include "notification_reactor_manager.hpp"
#include <algorithm>
#include <stdexcept>

void NotificationReactorManager::init(size_t total_devices) {
  constexpr size_t MAX_PER = 5000;
  size_t hw = std::thread::hardware_concurrency();
  if (!hw) hw = 4;
  size_t needed = (total_devices + MAX_PER - 1) / MAX_PER;
  set_reactor_count(std::min(hw, std::max<size_t>(1, needed)));
}

void NotificationReactorManager::set_reactor_count(size_t new_count) {
  std::lock_guard<std::mutex> lk(mtx_);

  // 1) gather all (fd,client) and unregister
  std::vector<std::pair<int,NetconfClient*>> all;
  all.reserve(fd_to_client_.size());
  for (auto const& [fd, client] : fd_to_client_) {
    size_t old_idx = fd_to_reactor_[fd];
    reactors_[old_idx]->remove(fd);
    all.emplace_back(fd, client);
  }

  // 2) clear state
  fd_to_reactor_.clear();
  fd_to_client_.clear();
  reactors_.clear();
  device_counts_.clear();

  // 3) rebuild 'new_count' reactors
  reactors_.reserve(new_count);
  device_counts_.assign(new_count, 0);
  for (size_t i = 0; i < new_count; ++i) {
    reactors_.emplace_back(std::make_unique<NotificationReactor>());
  }

  // 4) re-add all fds, always to the reactor with the fewest sockets
  for (auto const& [fd, client] : all) {
    auto best = std::min_element(device_counts_.begin(), device_counts_.end())
                - device_counts_.begin();
    reactors_[best]->add(fd, client);
    fd_to_reactor_[fd] = best;
    fd_to_client_[fd] = client;
    device_counts_[best]++;
  }
}

void NotificationReactorManager::add(int fd, NetconfClient* client) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (reactors_.empty()) {
    throw std::logic_error("NotificationReactorManager not initialized");
  }

  // pick the reactor with the minimum load
  auto best = std::min_element(device_counts_.begin(), device_counts_.end())
              - device_counts_.begin();

  reactors_[best]->add(fd, client);
  fd_to_reactor_[fd] = best;
  fd_to_client_[fd]  = client;
  device_counts_[best]++;
}

void NotificationReactorManager::remove(int fd) {
  std::lock_guard<std::mutex> lk(mtx_);
  auto it = fd_to_reactor_.find(fd);
  if (it == fd_to_reactor_.end()) return;

  size_t idx = it->second;
  reactors_[idx]->remove(fd);
  fd_to_reactor_.erase(it);
  fd_to_client_.erase(fd);
  device_counts_[idx]--;
}
