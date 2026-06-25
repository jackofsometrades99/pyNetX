#include "notification_event_bus.hpp"
#include "netconf_client.hpp"
#include "thread_pool.hpp"
#include "thread_pool_global.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

std::string current_notification_event_timestamp_utc() {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto now_time_t = system_clock::to_time_t(now);

    std::tm tm_utc{};
    gmtime_r(&now_time_t, &tm_utc);

    const auto ms = duration_cast<milliseconds>(
        now.time_since_epoch()
    ) % 1000;

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S")
        << '.'
        << std::setw(3)
        << std::setfill('0')
        << ms.count()
        << 'Z';

    return oss.str();
}

NotificationEventBus& NotificationEventBus::instance() {
    // Intentionally leaked to avoid static-destruction ordering issues while
    // Python may still hold references during interpreter shutdown.
    static NotificationEventBus* bus = new NotificationEventBus();
    return *bus;
}

void NotificationEventBus::emit(NotificationHealthEvent event) noexcept {
    try {
        if (event.timestamp.empty()) {
            event.timestamp = current_notification_event_timestamp_utc();
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);

            if (queue_.size() >= max_queue_size_) {
                queue_.pop_front();
                ++dropped_events_;
                event.health_events_dropped = dropped_events_;
            }

            queue_.push_back(std::move(event));
        }

        cv_.notify_one();

    } catch (const std::exception& e) {
        std::cerr << "NotificationEventBus: failed to emit event: "
                  << e.what() << std::endl;
    } catch (...) {
        std::cerr << "NotificationEventBus: failed to emit event: unknown error"
                  << std::endl;
    }
}

NotificationHealthEvent NotificationEventBus::next_event(int timeout_ms) {
    if (timeout_ms < -1) {
        throw NetconfException("timeout_ms must be -1 for infinite wait or >= 0");
    }

    std::unique_lock<std::mutex> lk(mtx_);

    bool got_event = false;

    if (timeout_ms < 0) {
        cv_.wait(lk, [this] { return !queue_.empty(); });
        got_event = true;
    } else if (timeout_ms == 0) {
        got_event = !queue_.empty();
    } else {
        got_event = cv_.wait_for(
            lk,
            std::chrono::milliseconds(timeout_ms),
            [this] { return !queue_.empty(); }
        );
    }

    if (!got_event) {
        NotificationHealthEvent timeout_event;
        timeout_event.valid = false;
        timeout_event.type = "timeout";
        timeout_event.timestamp = current_notification_event_timestamp_utc();
        timeout_event.label = "None";
        timeout_event.message = "No notification health event available before timeout";
        timeout_event.health_events_dropped = dropped_events_;
        return timeout_event;
    }

    NotificationHealthEvent event = std::move(queue_.front());
    queue_.pop_front();
    event.health_events_dropped = dropped_events_;
    return event;
}

std::future<NotificationHealthEvent> NotificationEventBus::next_event_async(int timeout_ms) {
    return get_pool().enqueue([timeout_ms]() -> NotificationHealthEvent {
        return NotificationEventBus::instance().next_event(timeout_ms);
    });
}

std::size_t NotificationEventBus::pending_event_count() {
    std::lock_guard<std::mutex> lk(mtx_);
    return queue_.size();
}

std::int64_t NotificationEventBus::dropped_event_count() {
    std::lock_guard<std::mutex> lk(mtx_);
    return dropped_events_;
}

void NotificationEventBus::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    queue_.clear();
    dropped_events_ = 0;
}