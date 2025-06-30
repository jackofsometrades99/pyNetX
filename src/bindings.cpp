#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "netconf_client.hpp"
#include "notification_reactor_manager.hpp"
#include "thread_pool.hpp"
#include "thread_pool_global.hpp"
#include <future>
#include <thread>
#include <iostream>
#include <libssh2.h>

namespace py = pybind11;

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

    std::thread([sfut = std::move(sfut), loop_ptr, py_future_ptr]() mutable {
        while (sfut.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        {
            py::gil_scoped_acquire acquire;
            try {
                T result = sfut.get();
                auto callback = py::cpp_function([py_future_ptr, result](py::args) {
                    (*py_future_ptr).attr("set_result")(result);
                });
                (*loop_ptr).attr("call_soon_threadsafe")(callback);
            } catch (const std::exception &e) {
                std::string msg = e.what();
                auto builtins = py::module::import("builtins");
                py::object exception_obj = builtins.attr("ValueError")(msg);
                auto callback = py::cpp_function([py_future_ptr, exception_obj](py::args) {
                    (*py_future_ptr).attr("set_exception")(exception_obj);
                });
                (*loop_ptr).attr("call_soon_threadsafe")(callback);
            }
            {
                py::gil_scoped_acquire acquire;
                loop_ptr.reset();
                py_future_ptr.reset();
            }
        }
    }).detach();
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

    std::thread([sfut = std::move(sfut), loop_ptr, py_future_ptr]() mutable {
        while (sfut.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        {
            py::gil_scoped_acquire acquire;
            try {
                sfut.get();
                auto callback = py::cpp_function([py_future_ptr](py::args) {
                    (*py_future_ptr).attr("set_result")(py::none());
                });
                (*loop_ptr).attr("call_soon_threadsafe")(callback);
            } catch (const std::exception &e) {
                std::string msg = e.what();
                auto builtins = py::module::import("builtins");
                py::object exception_obj = builtins.attr("ValueError")(msg);
                auto callback = py::cpp_function([py_future_ptr, exception_obj](py::args) {
                    (*py_future_ptr).attr("set_exception")(exception_obj);
                });
                (*loop_ptr).attr("call_soon_threadsafe")(callback);
            }
            {
                py::gil_scoped_acquire acquire;
                loop_ptr.reset();
                py_future_ptr.reset();
            }
        }
    }).detach();
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

    // Bind NetconfClient with shared_ptr for proper lifetime management.
    py::class_<NetconfClient, std::shared_ptr<NetconfClient>>(m, "NetconfClient")
        .def(py::init([](const std::string &hostname,
                         int port,
                         const std::string &username,
                         const std::string &password,
                         const std::string &key_path,
                         int connect_timeout,
                         int read_timeout) {
            return std::make_shared<NetconfClient>(
                hostname,
                port,
                username,
                password,
                key_path,
                connect_timeout,
                read_timeout
            );
        }),
        py::arg("hostname"),
        py::arg("port") = 830,
        py::arg("username"),
        py::arg("password"),
        py::arg("key_path") = "",
        py::arg("connect_timeout") = 60,
        py::arg("read_timeout") = 60)
        // Synchronous methods
        .def("connect_sync", &NetconfClient::connect_sync)
        .def("disconnect_sync", &NetconfClient::disconnect_sync)
        .def("delete_subscription", &NetconfClient::delete_notification_session)
        .def("send_rpc_sync", &NetconfClient::send_rpc_sync, py::arg("rpc"))
        .def("receive_notification_sync", &NetconfClient::receive_notification_sync)
        .def("get_sync", &NetconfClient::get_sync, py::arg("filter") = "")
        .def("get_config_sync", &NetconfClient::get_config_sync,
             py::arg("source") = "running", py::arg("filter") = "")
        .def("copy_config_sync", &NetconfClient::copy_config_sync,
             py::arg("target"), py::arg("source"))
        .def("delete_config_sync", &NetconfClient::delete_config_sync,
             py::arg("target"))
        .def("validate_sync", &NetconfClient::validate_sync,
             py::arg("source") = "running")
        .def("edit_config_sync", &NetconfClient::edit_config_sync,
             py::arg("target"), py::arg("config"), py::arg("do_validate") = false)
        .def("subscribe_sync", &NetconfClient::subscribe_sync,
             py::arg("stream") = "NETCONF", py::arg("filter") = "")
        .def("lock_sync", &NetconfClient::lock_sync, py::arg("target") = "running")
        .def("unlock_sync", &NetconfClient::unlock_sync, py::arg("target") = "running")
        .def("commit_sync", &NetconfClient::commit_sync)
        .def("locked_edit_config_sync", &NetconfClient::locked_edit_config_sync,
             py::arg("target"), py::arg("config"), py::arg("do_validate") = false)
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
        .def("next_notification", &NetconfClient::next_notification)
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
