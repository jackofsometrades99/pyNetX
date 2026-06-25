#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "netconf_client.hpp"
#include "notification_reactor_manager.hpp"
#include "notification_event_bus.hpp"
#include "thread_pool.hpp"
#include "thread_pool_global.hpp"
#include <future>
#include <thread>
#include <iostream>
#include <libssh2.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iterator>
#include <mutex>
#include <vector>

namespace py = pybind11;

inline void warn_sync_api_deprecated(const char* method_name) {
    std::string message =
        std::string("pyNetX.") + method_name +
        " is deprecated and will be removed in a future major release. "
        "pyNetX is moving to an async-focused API. "
        "Use the corresponding *_async method instead.";

    PyErr_WarnEx(PyExc_DeprecationWarning, message.c_str(), 2);
}

// ---- Register custom exceptions with Python
void register_exceptions(py::module_ &m) {
    static py::exception<NetconfConnectionRefused> connRefused(
        m, "NetconfConnectionRefusedError", PyExc_ConnectionError
    );
    static py::exception<NetconfAuthError> authErr(
        m, "NetconfAuthError", PyExc_PermissionError
    );
    static py::exception<NetconfChannelError> chanErr(
        m, "NetconfChannelError", PyExc_OSError
    );
    static py::exception<NetconfException> netconfBase(
        m, "NetconfException", PyExc_RuntimeError
    );
}


inline bool fut_pending(const py::object &f)
{
    return !(f.attr("done")().cast<bool>());
}


inline void reset_py_objects_safely(
    std::shared_ptr<py::object>& loop_ptr,
    std::shared_ptr<py::object>& py_future_ptr
) noexcept {
    try {
        py::gil_scoped_acquire acquire;
        loop_ptr.reset();
        py_future_ptr.reset();
    } catch (...) {
        // Never allow cleanup of Python references from a detached C++ thread
        // to terminate the process.
    }
}


inline py::object python_exception_from_cpp_exception(const std::exception& e) {
    py::module_ pyNetX = py::module_::import("pyNetX");

    if (dynamic_cast<const NetconfConnectionRefused*>(&e)) {
        return pyNetX.attr("NetconfConnectionRefusedError")(e.what());
    }
    if (dynamic_cast<const NetconfAuthError*>(&e)) {
        return pyNetX.attr("NetconfAuthError")(e.what());
    }
    if (dynamic_cast<const NetconfChannelError*>(&e)) {
        return pyNetX.attr("NetconfChannelError")(e.what());
    }
    if (dynamic_cast<const NetconfException*>(&e)) {
        return pyNetX.attr("NetconfException")(e.what());
    }

    auto builtins = py::module_::import("builtins");
    return builtins.attr("RuntimeError")(e.what());
}


namespace {

    constexpr auto ASYNC_FUTURE_POLL_INTERVAL = std::chrono::milliseconds(50);

    class AsyncFutureDispatcher {
    public:
        static AsyncFutureDispatcher& instance() {
            // Intentionally leaked: avoids Python-finalization order problems with
            // a global C++ object whose thread may still hold Python references.
            static AsyncFutureDispatcher* dispatcher = new AsyncFutureDispatcher();
            return *dispatcher;
        }

        void add(std::function<bool()> poller) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_.emplace_back(std::move(poller));
            }
            cv_.notify_one();
        }

    private:
        AsyncFutureDispatcher()
            : worker_([this]() { run(); })
        {
            // The dispatcher owns process-lifetime state through the leaked
            // singleton above. Detaching avoids a join during interpreter shutdown.
            worker_.detach();
        }

        void run() {
            std::vector<std::function<bool()>> batch;

            for (;;) {
                {
                    std::unique_lock<std::mutex> lock(mutex_);

                    if (pending_.empty()) {
                        cv_.wait(lock, [this]() { return !pending_.empty(); });
                    } else {
                        cv_.wait_for(lock, ASYNC_FUTURE_POLL_INTERVAL);
                    }

                    batch.swap(pending_);
                }

                std::vector<std::function<bool()>> still_pending;
                still_pending.reserve(batch.size());

                for (auto& poller : batch) {
                    bool done = true;

                    try {
                        done = poller();
                    } catch (const std::exception& e) {
                        std::cerr << "pyNetX async dispatcher dropped a poller: "
                                << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "pyNetX async dispatcher dropped a poller with unknown error"
                                << std::endl;
                    }

                    if (!done) {
                        still_pending.emplace_back(std::move(poller));
                    }
                }

                batch.clear();

                if (!still_pending.empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    pending_.insert(
                        pending_.end(),
                        std::make_move_iterator(still_pending.begin()),
                        std::make_move_iterator(still_pending.end())
                    );
                }
            }
        }

        std::mutex mutex_;
        std::condition_variable cv_;
        std::vector<std::function<bool()>> pending_;
        std::thread worker_;
    };

} // namespace


// ---- Utility: wrap std::future<T> into an asyncio Future ----
template <typename T>
py::object wrap_future(std::future<T> fut)
{
    std::shared_future<T> sfut = fut.share();
    py::object asyncio = py::module::import("asyncio");
    py::object loop = asyncio.attr("get_running_loop")();
    py::object py_future = loop.attr("create_future")();

    auto loop_ptr = std::make_shared<py::object>(loop);
    auto py_future_ptr = std::make_shared<py::object>(py_future);

    AsyncFutureDispatcher::instance().add(
        [sfut = std::move(sfut), loop_ptr, py_future_ptr]() mutable -> bool {
            if (sfut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                return false;
            }

            try {
                py::gil_scoped_acquire acquire;

                if (!fut_pending(*py_future_ptr)) {
                    reset_py_objects_safely(loop_ptr, py_future_ptr);
                    return true;
                }

                try {
                    T result = sfut.get();

                    auto callback = py::cpp_function(
                        [py_future_ptr, result](py::args) {
                            if (fut_pending(*py_future_ptr)) {
                                (*py_future_ptr).attr("set_result")(result);
                            }
                        }
                    );

                    try {
                        (*loop_ptr).attr("call_soon_threadsafe")(callback);
                    } catch (const std::exception& e) {
                        std::cerr << "pyNetX: call_soon_threadsafe(set_result) failed: "
                                  << e.what() << std::endl;
                    }

                } catch (const std::exception& e) {
                    py::object exception_obj = python_exception_from_cpp_exception(e);

                    auto callback = py::cpp_function(
                        [py_future_ptr, exception_obj](py::args) {
                            if (fut_pending(*py_future_ptr)) {
                                (*py_future_ptr).attr("set_exception")(exception_obj);
                            }
                        }
                    );

                    try {
                        (*loop_ptr).attr("call_soon_threadsafe")(callback);
                    } catch (const std::exception& inner) {
                        std::cerr << "pyNetX: call_soon_threadsafe(set_exception) failed: "
                                  << inner.what() << std::endl;
                    }
                }

                reset_py_objects_safely(loop_ptr, py_future_ptr);
                return true;

            } catch (const std::exception& e) {
                std::cerr << "pyNetX async dispatcher failed to complete future: "
                          << e.what() << std::endl;
                reset_py_objects_safely(loop_ptr, py_future_ptr);
                return true;
            } catch (...) {
                std::cerr << "pyNetX async dispatcher failed to complete future with unknown exception"
                          << std::endl;
                reset_py_objects_safely(loop_ptr, py_future_ptr);
                return true;
            }
        }
    );

    return py_future;
}


// Specialization for std::future<void>
template <>
py::object wrap_future<void>(std::future<void> fut)
{
    std::shared_future<void> sfut = fut.share();
    py::object asyncio = py::module::import("asyncio");
    py::object loop = asyncio.attr("get_running_loop")();
    py::object py_future = loop.attr("create_future")();

    auto loop_ptr = std::make_shared<py::object>(loop);
    auto py_future_ptr = std::make_shared<py::object>(py_future);

    AsyncFutureDispatcher::instance().add(
        [sfut = std::move(sfut), loop_ptr, py_future_ptr]() mutable -> bool {
            if (sfut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                return false;
            }

            try {
                py::gil_scoped_acquire acquire;

                if (!fut_pending(*py_future_ptr)) {
                    reset_py_objects_safely(loop_ptr, py_future_ptr);
                    return true;
                }

                try {
                    sfut.get();

                    auto callback = py::cpp_function(
                        [py_future_ptr](py::args) {
                            if (fut_pending(*py_future_ptr)) {
                                (*py_future_ptr).attr("set_result")(py::none());
                            }
                        }
                    );

                    try {
                        (*loop_ptr).attr("call_soon_threadsafe")(callback);
                    } catch (const std::exception& e) {
                        std::cerr << "pyNetX: call_soon_threadsafe(set_result void) failed: "
                                  << e.what() << std::endl;
                    }

                } catch (const std::exception& e) {
                    py::object exception_obj = python_exception_from_cpp_exception(e);

                    auto callback = py::cpp_function(
                        [py_future_ptr, exception_obj](py::args) {
                            if (fut_pending(*py_future_ptr)) {
                                (*py_future_ptr).attr("set_exception")(exception_obj);
                            }
                        }
                    );

                    try {
                        (*loop_ptr).attr("call_soon_threadsafe")(callback);
                    } catch (const std::exception& inner) {
                        std::cerr << "pyNetX: call_soon_threadsafe(set_exception void) failed: "
                                  << inner.what() << std::endl;
                    }
                }

                reset_py_objects_safely(loop_ptr, py_future_ptr);
                return true;

            } catch (const std::exception& e) {
                std::cerr << "pyNetX async dispatcher failed to complete void future: "
                          << e.what() << std::endl;
                reset_py_objects_safely(loop_ptr, py_future_ptr);
                return true;
            } catch (...) {
                std::cerr << "pyNetX async dispatcher failed to complete void future with unknown exception"
                          << std::endl;
                reset_py_objects_safely(loop_ptr, py_future_ptr);
                return true;
            }
        }
    );

    return py_future;
}


PYBIND11_MODULE(pyNetX, m) {
    int rc = libssh2_init(0);
    if (rc != 0) {
        throw std::runtime_error("libssh2_init() failed!");
    }
    m.def("set_threadpool_size", [](int n){
        init_global_pool(n);
    }, py::arg("n"),
    "Set the size of the global thread pool for all NetconfClient async operations."
    );
    m.def("set_notification_reactor_count",
        [](size_t n){
            NotificationReactorManager::instance().set_reactor_count(n);
        },
        py::arg("num_reactors"),
        "Reconfigure the number of notification-reactor threads on the fly."
    );
    m.doc() = "NETCONF client with async non blocking capabilities.";

    register_exceptions(m);

    py::class_<NotificationHealthEvent>(m, "NotificationHealthEvent")
        .def_readonly("valid", &NotificationHealthEvent::valid)
        .def_readonly("type", &NotificationHealthEvent::type)
        .def_readonly("timestamp", &NotificationHealthEvent::timestamp)
        .def_readonly("label", &NotificationHealthEvent::label)
        .def_readonly("hostname", &NotificationHealthEvent::hostname)
        .def_readonly("port", &NotificationHealthEvent::port)
        .def_readonly("fd", &NotificationHealthEvent::fd)
        .def_readonly("message", &NotificationHealthEvent::message)
        .def_readonly("queue_size", &NotificationHealthEvent::queue_size)
        .def_readonly("queue_max_size", &NotificationHealthEvent::queue_max_size)
        .def_readonly("queue_high_watermark", &NotificationHealthEvent::queue_high_watermark)
        .def_readonly("notifications_enqueued", &NotificationHealthEvent::notifications_enqueued)
        .def_readonly("notifications_dropped_queue_full", &NotificationHealthEvent::notifications_dropped_queue_full)
        .def_readonly("notifications_dropped_delta", &NotificationHealthEvent::notifications_dropped_delta)
        .def_readonly("incomplete_notifications_received", &NotificationHealthEvent::incomplete_notifications_received)
        .def_readonly("partial_bytes", &NotificationHealthEvent::partial_bytes)
        .def_readonly("health_events_dropped", &NotificationHealthEvent::health_events_dropped)
        .def("as_dict", [](const NotificationHealthEvent& event) {
            py::dict doc;
            doc["valid"] = event.valid;
            doc["type"] = event.type;
            doc["timestamp"] = event.timestamp;
            doc["label"] = event.label;
            doc["hostname"] = event.hostname;
            doc["port"] = event.port;
            doc["fd"] = event.fd;
            doc["message"] = event.message;
            doc["queue_size"] = event.queue_size;
            doc["queue_max_size"] = event.queue_max_size;
            doc["queue_high_watermark"] = event.queue_high_watermark;
            doc["notifications_enqueued"] = event.notifications_enqueued;
            doc["notifications_dropped_queue_full"] = event.notifications_dropped_queue_full;
            doc["notifications_dropped_delta"] = event.notifications_dropped_delta;
            doc["incomplete_notifications_received"] = event.incomplete_notifications_received;
            doc["partial_bytes"] = event.partial_bytes;
            doc["health_events_dropped"] = event.health_events_dropped;
            return doc;
        });

    m.def("next_notification_event", [](int timeout_ms) {
        py::gil_scoped_release release;
        return NotificationEventBus::instance().next_event(timeout_ms);
    }, py::arg("timeout_ms") = -1);

    m.def("next_notification_event_async", [](int timeout_ms) {
        return wrap_future(NotificationEventBus::instance().next_event_async(timeout_ms));
    }, py::arg("timeout_ms") = -1);

    m.def("pending_notification_event_count", []() {
        py::gil_scoped_release release;
        return NotificationEventBus::instance().pending_event_count();
    });

    m.def("clear_notification_events", []() {
        py::gil_scoped_release release;
        NotificationEventBus::instance().clear();
    });

    // Bind NetconfClient with shared_ptr for proper lifetime management.
    py::class_<NetconfClient, std::shared_ptr<NetconfClient>>(m, "NetconfClient")
        .def(py::init([](const std::string &hostname,
                         int port,
                         const std::string &username,
                         const std::string &password,
                         const std::string &key_path,
                         int connect_timeout,
                         int read_timeout,
                         int notif_queue_size,
                         int socket_connect_timeout,
                         int notif_incomplete_max_kb,
                         int notif_incomplete_timeout,
                         int notif_drop_event_threshold,
                         const std::string& label) {
            return std::make_shared<NetconfClient>(
                hostname,
                port,
                username,
                password,
                key_path,
                connect_timeout,
                read_timeout,
                notif_queue_size,
                socket_connect_timeout,
                notif_incomplete_max_kb,
                notif_incomplete_timeout,
                notif_drop_event_threshold,
                label
            );
        }),
        py::arg("hostname"),
        py::arg("port") = 830,
        py::arg("username"),
        py::arg("password"),
        py::arg("key_path") = "",
        py::arg("connect_timeout") = 60,
        py::arg("read_timeout") = 60,
        py::arg("notif_queue_size") = -1,
        py::arg("socket_connect_timeout") = 5,
        py::arg("notif_incomplete_max_kb") = 1024,
        py::arg("notif_incomplete_timeout") = 5,
        py::arg("notif_drop_event_threshold") = 1,
        py::arg("label") = "None")
        // Synchronous methods
        // Deprecated synchronous flow methods.
        // These remain available in 2.0.5 for compatibility, but pyNetX is
        // moving toward an async-focused API.
        .def("connect_sync", [](NetconfClient& self) {
            warn_sync_api_deprecated("connect_sync");
            return self.connect_sync();
        })
        .def("disconnect_sync", [](NetconfClient& self) {
            warn_sync_api_deprecated("disconnect_sync");
            return self.disconnect_sync();
        })
        .def("delete_subscription", &NetconfClient::delete_notification_session)
        .def("send_rpc_sync", [](NetconfClient& self, const std::string& rpc) {
            warn_sync_api_deprecated("send_rpc_sync");
            return self.send_rpc_sync(rpc);
        }, py::arg("rpc"))
        .def("receive_notification_sync", [](NetconfClient& self) {
            warn_sync_api_deprecated("receive_notification_sync");
            return self.receive_notification_sync();
        })
        .def("get_sync", [](NetconfClient& self, const std::string& filter) {
            warn_sync_api_deprecated("get_sync");
            return self.get_sync(filter);
        }, py::arg("filter") = "")
        .def("get_config_sync", [](
            NetconfClient& self,
            const std::string& source,
            const std::string& filter
        ) {
            warn_sync_api_deprecated("get_config_sync");
            return self.get_config_sync(source, filter);
        }, py::arg("source") = "running", py::arg("filter") = "")
        .def("copy_config_sync", [](
            NetconfClient& self,
            const std::string& target,
            const std::string& source
        ) {
            warn_sync_api_deprecated("copy_config_sync");
            return self.copy_config_sync(target, source);
        }, py::arg("target"), py::arg("source"))
        .def("delete_config_sync", [](NetconfClient& self, const std::string& target) {
            warn_sync_api_deprecated("delete_config_sync");
            return self.delete_config_sync(target);
        }, py::arg("target"))
        .def("validate_sync", [](NetconfClient& self, const std::string& source) {
            warn_sync_api_deprecated("validate_sync");
            return self.validate_sync(source);
        }, py::arg("source") = "running")
        .def("edit_config_sync", [](
            NetconfClient& self,
            const std::string& target,
            const std::string& config,
            bool do_validate
        ) {
            warn_sync_api_deprecated("edit_config_sync");
            return self.edit_config_sync(target, config, do_validate);
        }, py::arg("target"), py::arg("config"), py::arg("do_validate") = false)
        .def("subscribe_sync", [](
            NetconfClient& self,
            const std::string& stream,
            const std::string& filter
        ) {
            warn_sync_api_deprecated("subscribe_sync");
            return self.subscribe_sync(stream, filter);
        }, py::arg("stream") = "NETCONF", py::arg("filter") = "")
        .def("lock_sync", [](NetconfClient& self, const std::string& target) {
            warn_sync_api_deprecated("lock_sync");
            return self.lock_sync(target);
        }, py::arg("target") = "running")
        .def("unlock_sync", [](NetconfClient& self, const std::string& target) {
            warn_sync_api_deprecated("unlock_sync");
            return self.unlock_sync(target);
        }, py::arg("target") = "running")
        .def("commit_sync", [](NetconfClient& self) {
            warn_sync_api_deprecated("commit_sync");
            return self.commit_sync();
        })
        .def("locked_edit_config_sync", [](
            NetconfClient& self,
            const std::string& target,
            const std::string& config,
            bool do_validate
        ) {
            warn_sync_api_deprecated("locked_edit_config_sync");
            return self.locked_edit_config_sync(target, config, do_validate);
        }, py::arg("target"), py::arg("config"), py::arg("do_validate") = false)
        // Asynchronous methods
        .def("connect_async", [](std::shared_ptr<NetconfClient> &self) {
            return wrap_future(self->connect_async());
        })
        .def("disconnect_async", [](std::shared_ptr<NetconfClient> &self) {
            return wrap_future(self->disconnect_async());
        })
        .def("send_rpc_async", [](std::shared_ptr<NetconfClient> &self, const std::string &rpc) {
            return wrap_future(self->send_rpc_async(rpc));
        }, py::arg("rpc"))
        .def("next_notification", &NetconfClient::next_notification,
            py::arg("timeout_ms") = 10,
            py::call_guard<py::gil_scoped_release>())
        .def("next_notification_async", [](
            std::shared_ptr<NetconfClient> &self,
            int timeout_ms
        ) {
            return wrap_future(self->next_notification_async(timeout_ms));
        }, py::arg("timeout_ms") = 10)
        .def("peek_notifications", [](NetconfClient& self, int max_items) {
            py::gil_scoped_release release;
            return self.peek_notifications(max_items);
        }, py::arg("max_items") = 100)
        .def("notification_queue_size", [](NetconfClient& self) {
            py::gil_scoped_release release;
            return self.notification_queue_size();
        })
        .def("is_subscription_active", &NetconfClient::is_subscription_active)
        .def("get_async", [](std::shared_ptr<NetconfClient> &self, const std::string &filter) {
            return wrap_future(self->get_async(filter));
        }, py::arg("filter") = "")
        .def("get_config_async", [](std::shared_ptr<NetconfClient> &self,
                                    const std::string &source,
                                    const std::string &filter){
            return wrap_future(self->get_config_async(source, filter));
        }, py::arg("source") = "running", py::arg("filter") = "")
        .def("copy_config_async", [](std::shared_ptr<NetconfClient> &self,
                                     const std::string &target,
                                     const std::string &source){
            return wrap_future(self->copy_config_async(target, source));
        })
        .def("delete_config_async", [](std::shared_ptr<NetconfClient> &self,
                                       const std::string &target){
            return wrap_future(self->delete_config_async(target));
        })
        .def("validate_async", [](std::shared_ptr<NetconfClient> &self,
                                  const std::string &source){
            return wrap_future(self->validate_async(source));
        }, py::arg("source") = "running")
        .def("edit_config_async", [](std::shared_ptr<NetconfClient> &self,
                                     const std::string &target,
                                     const std::string &config,
                                     bool do_validate){
            return wrap_future(self->edit_config_async(target, config, do_validate));
        }, py::arg("target"), py::arg("config"), py::arg("do_validate") = false)
        .def("subscribe_async", [](std::shared_ptr<NetconfClient> &self,
                                   const std::string &stream,
                                   const std::string &filter){
            return wrap_future(self->subscribe_async(stream, filter));
        }, py::arg("stream") = "NETCONF", py::arg("filter") = "")
        .def("lock_async", [](std::shared_ptr<NetconfClient> &self,
                              const std::string &target){
            return wrap_future(self->lock_async(target));
        }, py::arg("target") = "running")
        .def("unlock_async", [](std::shared_ptr<NetconfClient> &self,
                                const std::string &target){
            return wrap_future(self->unlock_async(target));
        }, py::arg("target") = "running")
        .def("commit_async", [](std::shared_ptr<NetconfClient> &self){
            return wrap_future(self->commit_async());
        })
        .def("locked_edit_config_async", [](std::shared_ptr<NetconfClient> &self,
                                            const std::string &target,
                                            const std::string &config,
                                            bool do_validate){
            return wrap_future(self->locked_edit_config_async(target, config, do_validate));
        }, py::arg("target"), py::arg("config"), py::arg("do_validate") = false)
    ;
}
