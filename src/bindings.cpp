#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "netconf_client.hpp"
#include <future>
#include <thread>

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
    // Ensure a running event loop is available
    py::object asyncio = py::module::import("asyncio");
    py::object loop = asyncio.attr("get_running_loop")();
    py::object py_future = loop.attr("create_future")();

    // Launch a thread to wait on the std::future and then set the result
    std::thread([fut = std::move(fut), loop, py_future]() mutable {
        try {
            T result = fut.get();
            py::gil_scoped_acquire acquire; // reacquire GIL
            auto callback = py::cpp_function([py_future, result]() {
                py_future.attr("set_result")(result);
            });
            loop.attr("call_soon_threadsafe")(callback);
        }
        catch (const std::exception &e) {
            py::gil_scoped_acquire acquire;
            std::string msg = e.what();
            auto exception_obj = py::value_error(msg);
            auto callback = py::cpp_function([py_future, exception_obj]() {
                py_future.attr("set_exception")(exception_obj);
            });
            loop.attr("call_soon_threadsafe")(callback);
        }
    }).detach();

    return py_future;
}

// Specialization for std::future<void>
template <>
py::object wrap_future<void>(std::future<void> fut)
{
    py::object asyncio = py::module::import("asyncio");
    py::object loop = asyncio.attr("get_running_loop")();
    py::object py_future = loop.attr("create_future")();

    std::thread([fut = std::move(fut), loop, py_future]() mutable {
        try {
            fut.get();
            py::gil_scoped_acquire acquire;
            auto callback = py::cpp_function([py_future]() {
                py_future.attr("set_result")(py::none());
            });
            loop.attr("call_soon_threadsafe")(callback);
        }
        catch (const std::exception &e) {
            py::gil_scoped_acquire acquire;
            std::string msg = e.what();
            auto exception_obj = py::value_error(msg);
            auto callback = py::cpp_function([py_future, exception_obj]() {
                py_future.attr("set_exception")(exception_obj);
            });
            loop.attr("call_soon_threadsafe")(callback);
        }
    }).detach();

    return py_future;
}

PYBIND11_MODULE(pyNetX, m) {
    m.doc() = "NETCONF client with async capabilities and improved callback handling";

    register_exceptions(m);

    // Bind NetconfClient with shared_ptr for proper lifetime management.
    py::class_<NetconfClient, std::shared_ptr<NetconfClient>>(m, "NetconfClient")
        .def(py::init([](const std::string &hostname,
                         int port,
                         const std::string &username,
                         const std::string &password,
                         const std::string &key_path) {
            return std::make_shared<NetconfClient>(hostname, port, username, password, key_path);
        }),
        py::arg("hostname"),
        py::arg("port") = 830,
        py::arg("username"),
        py::arg("password"),
        py::arg("key_path") = "")
        // Synchronous methods
        .def("connect", &NetconfClient::connect)
        .def("disconnect", &NetconfClient::disconnect)
        .def("send_rpc", &NetconfClient::send_rpc, py::arg("rpc"))
        .def("receive_notification", &NetconfClient::receive_notification)
        .def("get", &NetconfClient::get, py::arg("filter") = "")
        .def("get_config", &NetconfClient::get_config,
             py::arg("source") = "running", py::arg("filter") = "")
        .def("copy_config", &NetconfClient::copy_config,
             py::arg("target"), py::arg("source"))
        .def("delete_config", &NetconfClient::delete_config,
             py::arg("target"))
        .def("validate", &NetconfClient::validate,
             py::arg("source") = "running")
        .def("edit_config", &NetconfClient::edit_config,
             py::arg("target"), py::arg("config"), py::arg("do_validate") = false)
        .def("subscribe", &NetconfClient::subscribe,
             py::arg("stream") = "NETCONF", py::arg("filter") = "")
        .def("lock", &NetconfClient::lock, py::arg("target") = "running")
        .def("unlock", &NetconfClient::unlock, py::arg("target") = "running")
        .def("commit", &NetconfClient::commit)
        .def("locked_edit_config", &NetconfClient::locked_edit_config,
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
        .def("receive_notification_async", [](std::shared_ptr<NetconfClient> &self) {
            return wrap_future(self->receive_notification_async());
        })
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
