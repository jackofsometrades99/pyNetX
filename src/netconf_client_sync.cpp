#include "netconf_client.hpp"
#include "thread_pool.hpp"
#include "thread_pool_global.hpp"
#include <memory>
#include <mutex>
#include <stdexcept>

// ----------------------- Synchronous Methods -----------------------

bool NetconfClient::connect_sync() {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self]() -> bool {
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->connect_blocking();
        }
    );
    return fut.get();
}

void NetconfClient::disconnect_sync() {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self]() -> void {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->disconnect();
        }
    );
    fut.get();
}

void NetconfClient::delete_subsription() {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self]() -> void {
            if (!self->notif_is_connected_) {
                throw NetconfException("Client should be subscribed first");
            }
            if (!self->notif_is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->delete_notification_session();
        }
    );
    fut.get();
}

std::string NetconfClient::send_rpc_sync(const std::string& rpc) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, rpc]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->send_rpc_blocking(rpc);
        }
    );
    return fut.get();
}

std::string NetconfClient::receive_notification_sync() {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->receive_notification_blocking();
        }
    );
    return fut.get();
}

std::string NetconfClient::get_sync(const std::string& filter) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, filter]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->get_blocking(filter);
        }
    );
    return fut.get();
}

std::string NetconfClient::get_config_sync(
    const std::string& source,
    const std::string& filter) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, source, filter]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->get_config_blocking(source, filter);
        }
    );
    return fut.get();
}

std::string NetconfClient::copy_config_sync(
    const std::string& target,
    const std::string& source) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, target, source]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->copy_config_blocking(target, source);
        }
    );
    return fut.get();
}

std::string NetconfClient::delete_config_sync(const std::string& target) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, target]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->delete_config_blocking(target);
        }
    );
    return fut.get();
}

std::string NetconfClient::validate_sync(const std::string& source) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, source]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->validate_blocking(source);
        }
    );
    return fut.get();
}

std::string NetconfClient::edit_config_sync(
    const std::string& target,
    const std::string& config,
    bool do_validate) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, target, config, do_validate]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->edit_config_blocking(target, config, do_validate);
        }
    );
    return fut.get();
}

std::string NetconfClient::subscribe_sync(
    const std::string& stream,
    const std::string& filter) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, stream, filter]() -> std::string {
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->subscribe_blocking(stream, filter);
        }
    );
    return fut.get();
}

std::string NetconfClient::lock_sync(const std::string& target) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, target]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->lock_blocking(target);
        }
    );
    return fut.get();
}

std::string NetconfClient::unlock_sync(const std::string& target) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, target]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->unlock_blocking(target);
        }
    );
    return fut.get();
}

std::string NetconfClient::commit_sync() {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->commit_blocking();
        }
    );
    return fut.get();
}

std::string NetconfClient::locked_edit_config_sync(
    const std::string& target,
    const std::string& config,
    bool do_validate) {
    auto self = shared_from_this();
    auto fut = get_pool().enqueue([self, target, config, do_validate]() -> std::string {
            if (!self->is_connected_) {
                throw NetconfException("Client should be connected first");
            }
            if (!self->is_blocking_) {
                throw NetconfException("Client is connected asynchronously, call asynchronous methods");
            }
            std::unique_lock<std::mutex> lock(self->session_mutex_);
            return self->locked_edit_config_blocking(target, config, do_validate);
        }
    );
    return fut.get();
}