#include "notification_reactor_manager.hpp"
#include "netconf_client.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

void NotificationReactorManager::init(size_t total_devices) {
    constexpr size_t MAX_PER = 5000;

    size_t hw = std::thread::hardware_concurrency();
    if (!hw) {
        hw = 4;
    }

    size_t needed = (total_devices + MAX_PER - 1) / MAX_PER;
    set_reactor_count(std::min(hw, std::max<size_t>(1, needed)));
}

void NotificationReactorManager::set_reactor_count(size_t new_count) {
    std::lock_guard<std::mutex> lk(mtx_);

    if (new_count == 0) {
        throw std::invalid_argument("new_count must be greater than 0");
    }

    std::vector<std::pair<int, std::weak_ptr<NetconfClient>>> all;
    all.reserve(fd_to_client_.size());

    for (const auto& entry : fd_to_client_) {
        int fd = entry.first;
        const auto& weak_client = entry.second;

        auto reactor_it = fd_to_reactor_.find(fd);
        if (reactor_it != fd_to_reactor_.end()) {
            size_t old_idx = reactor_it->second;
            if (old_idx < reactors_.size() && reactors_[old_idx]) {
                reactors_[old_idx]->remove(fd);
            }
        }

        if (!weak_client.expired()) {
            all.emplace_back(fd, weak_client);
        }
    }

    fd_to_reactor_.clear();
    fd_to_client_.clear();
    reactors_.clear();
    device_counts_.clear();

    reactors_.reserve(new_count);
    device_counts_.assign(new_count, 0);

    for (size_t i = 0; i < new_count; ++i) {
        reactors_.emplace_back(std::make_unique<NotificationReactor>());
    }

    for (const auto& entry : all) {
        int fd = entry.first;
        std::weak_ptr<NetconfClient> weak_client = entry.second;

        if (weak_client.expired()) {
            continue;
        }

        size_t best =
            static_cast<size_t>(
                std::min_element(device_counts_.begin(), device_counts_.end()) -
                device_counts_.begin()
            );

        reactors_[best]->add(fd, weak_client);
        fd_to_reactor_[fd] = best;
        fd_to_client_[fd] = weak_client;
        device_counts_[best]++;
    }
}

void NotificationReactorManager::add(
    int fd,
    std::shared_ptr<NetconfClient> client
) {
    if (!client) {
        throw std::invalid_argument("NotificationReactorManager::add got null client");
    }

    std::lock_guard<std::mutex> lk(mtx_);

    // Instead of throwing an error when add is called before init, we can just initialize with a single reactor.
    // if (reactors_.empty()) {
    //     throw std::logic_error("NotificationReactorManager not initialized");
    // }

    if (reactors_.empty()) {
        reactors_.emplace_back(std::make_unique<NotificationReactor>());
        device_counts_.push_back(0);
    }

    std::weak_ptr<NetconfClient> weak_client = client;

    size_t best =
        static_cast<size_t>(
            std::min_element(device_counts_.begin(), device_counts_.end()) -
            device_counts_.begin()
        );

    reactors_[best]->add(fd, weak_client);
    fd_to_reactor_[fd] = best;
    fd_to_client_[fd] = weak_client;
    device_counts_[best]++;
}

void NotificationReactorManager::remove(int fd) {
    std::lock_guard<std::mutex> lk(mtx_);

    auto it = fd_to_reactor_.find(fd);
    if (it == fd_to_reactor_.end()) {
        return;
    }

    size_t idx = it->second;

    if (idx < reactors_.size() && reactors_[idx]) {
        reactors_[idx]->remove(fd);
    }

    fd_to_reactor_.erase(it);
    fd_to_client_.erase(fd);

    if (idx < device_counts_.size() && device_counts_[idx] > 0) {
        device_counts_[idx]--;
    }
}