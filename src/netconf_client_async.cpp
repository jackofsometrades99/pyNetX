#include "netconf_client.hpp"
#include "thread_pool.hpp"
#include "thread_pool_global.hpp"
#include <memory>
#include <mutex>
#include <stdexcept>
#include <future>


// ----------------------- Asynchronous Methods -----------------------
std::future<bool> NetconfClient::connect_async() {
    auto self = shared_from_this();
    return get_pool().enqueue([self]() -> bool {
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->connect_non_blocking();
    });
}

std::future<void> NetconfClient::disconnect_async() {
    auto self = shared_from_this();
    return get_pool().enqueue([self]() -> void {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        self->disconnect();
    });
}

std::future<std::string> NetconfClient::send_rpc_async(const std::string& rpc) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, rpc]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->send_rpc_non_blocking(rpc);
    });
}

std::future<std::string> NetconfClient::get_async(const std::string& filter) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, filter]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->get_non_blocking(filter);
    });
}

std::future<std::string> NetconfClient::get_config_async(const std::string& source,
                                                         const std::string& filter) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, source, filter]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->get_config_non_blocking(source, filter);
    });
}

std::future<std::string> NetconfClient::copy_config_async(const std::string& target,
                                                          const std::string& source) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, target, source]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->copy_config_non_blocking(target, source);
    });
}

std::future<std::string> NetconfClient::delete_config_async(const std::string& target) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, target]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->delete_config_non_blocking(target);
    });
}

std::future<std::string> NetconfClient::validate_async(const std::string& source) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, source]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->validate_non_blocking(source);
    });
}

std::future<std::string> NetconfClient::edit_config_async(const std::string& target,
                                                          const std::string& config,
                                                          bool do_validate) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, target, config, do_validate]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->edit_config_non_blocking(target, config, do_validate);
    });
}

std::future<std::string> NetconfClient::subscribe_async(const std::string& stream,
                                                        const std::string& filter) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, stream, filter]() -> std::string {
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->subscribe_non_blocking(stream, filter);
    });
}

std::future<std::string> NetconfClient::lock_async(const std::string& target) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, target]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->lock_non_blocking(target);
    });
}

std::future<std::string> NetconfClient::unlock_async(const std::string& target) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, target]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->unlock_non_blocking(target);
    });
}

std::future<std::string> NetconfClient::commit_async() {
    auto self = shared_from_this();
    return get_pool().enqueue([self]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->commit_non_blocking();
    });
}

std::future<std::string> NetconfClient::locked_edit_config_async(const std::string& target,
                                                                 const std::string& config,
                                                                 bool do_validate) {
    auto self = shared_from_this();
    return get_pool().enqueue([self, target, config, do_validate]() -> std::string {
        if (!self->is_connected_) {
            throw NetconfException("Client already not connected");
        }
        if (self->is_blocking_) {
            throw NetconfException("Client is connected synchronously, call synchronous methods");
        }
        std::unique_lock<std::mutex> lock(self->session_mutex_);
        return self->locked_edit_config_non_blocking(target, config, do_validate);
    });
}