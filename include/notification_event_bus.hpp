#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <mutex>
#include <string>

struct NotificationHealthEvent {
    bool valid = true;
    std::string type;
    std::string hostname;
    int port = 0;
    int fd = -1;
    std::string message;

    std::int64_t queue_size = 0;
    std::int64_t queue_max_size = -1;
    std::int64_t queue_high_watermark = 0;
    std::int64_t notifications_enqueued = 0;
    std::int64_t notifications_dropped_queue_full = 0;
    std::int64_t notifications_dropped_delta = 0;
    std::int64_t incomplete_notifications_received = 0;
    std::int64_t partial_bytes = 0;
    std::int64_t health_events_dropped = 0;
};

class NotificationEventBus {
public:
    static NotificationEventBus& instance();

    void emit(NotificationHealthEvent event) noexcept;

    NotificationHealthEvent next_event(int timeout_ms = -1);
    std::future<NotificationHealthEvent> next_event_async(int timeout_ms = -1);

    std::size_t pending_event_count();
    std::int64_t dropped_event_count();
    void clear();

private:
    NotificationEventBus() = default;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<NotificationHealthEvent> queue_;
    std::int64_t dropped_events_ = 0;
    std::size_t max_queue_size_ = 10000;
};