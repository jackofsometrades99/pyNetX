#include "netconf_client.hpp"
#include <stdexcept>
#include <iostream>
#include <future>
#include <sstream>
#include <libssh2.h>
#include <tinyxml2.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>

// ----------------------- XML Error Checker -------------------------
void NetconfClient::check_for_rpc_error(const std::string& xml_reply) {
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError error = doc.Parse(xml_reply.c_str());
    if (error != tinyxml2::XML_SUCCESS) {
        // Ignoring parse error.
        return;
    }
    tinyxml2::XMLElement* rpcReply = doc.FirstChildElement("rpc-reply");
    if (!rpcReply) return;
    tinyxml2::XMLElement* rpcErr = rpcReply->FirstChildElement("rpc-error");
    if (!rpcErr) return;
    const char* errMsg = nullptr;
    auto* errElem = rpcErr->FirstChildElement("error-message");
    if (errElem) {
        errMsg = errElem->GetText();
    }
    if (!errMsg) {
        errMsg = "RPC error (unknown error-message)";
    }
    throw NetconfException(std::string("RPC error: ") + errMsg);
}

// ----------------------- Hostname Resolution Helpers -------------------------

std::string NetconfClient::resolve_hostname_blocking(
    const std::string &hostname
)   {
        try {
            // Blocking version: simply call getaddrinfo and return the first IP address found.
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
            hints.ai_socktype = SOCK_STREAM;
            
            struct addrinfo *res = nullptr;
            int err = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
            if (err != 0 || res == nullptr) {
                return "";
            }

            char ip[INET6_ADDRSTRLEN];
            if (res->ai_family == AF_INET) {
                struct sockaddr_in *addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
                inet_ntop(AF_INET, &(addr->sin_addr), ip, sizeof(ip));
            } else if (res->ai_family == AF_INET6) {
                struct sockaddr_in6 *addr6 = reinterpret_cast<struct sockaddr_in6*>(res->ai_addr);
                inet_ntop(AF_INET6, &(addr6->sin6_addr), ip, sizeof(ip));
            } else {
                freeaddrinfo(res);
                return "";
            }
            
            std::string result(ip);
            freeaddrinfo(res);
            return result;
        } catch (const std::exception& e) {
            throw NetconfException("Error occured while resolving hostname: " + std::string(e.what()));
        }
}

std::string NetconfClient::resolve_hostname_non_blocking(
    const std::string &hostname,
    int timeout_seconds
) {
    try {
        std::promise<std::string> prom;
        auto fut = prom.get_future();
        // Capture p by value (using move) so the thread has its own copy of the promise.
        std::thread resolver([p = std::move(prom), hostname]() mutable {
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
            hints.ai_socktype = SOCK_STREAM;
            struct addrinfo *res = nullptr;
            int err = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
            if (err == 0 && res != nullptr) {
                char ip[INET6_ADDRSTRLEN];
                // Use the first result; check for IPv4 or IPv6.
                if (res->ai_family == AF_INET) {
                    struct sockaddr_in *addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
                    inet_ntop(AF_INET, &(addr->sin_addr), ip, sizeof(ip));
                } else if (res->ai_family == AF_INET6) {
                    struct sockaddr_in6 *addr6 = reinterpret_cast<struct sockaddr_in6*>(res->ai_addr);
                    inet_ntop(AF_INET6, &(addr6->sin6_addr), ip, sizeof(ip));
                } else {
                    freeaddrinfo(res);
                    p.set_value("");
                    return;
                }
                freeaddrinfo(res);
                p.set_value(std::string(ip));
            } else {
                p.set_value("");
            }
        });
        // Wait for the resolution to complete or timeout.
        if (fut.wait_for(std::chrono::seconds(timeout_seconds)) == std::future_status::ready) {
            resolver.join();
            return fut.get();
        } else {
            resolver.detach(); // Let it run; we won’t use its result.
            return "";
        }
    } catch (const std::exception& e) {
        throw NetconfException("Error occured while resolving hostname: " + std::string(e.what()));
    }
}

// ----------------------- Polling Helpers -------------------------

namespace {
    constexpr const char* NETCONF_EOM = "]]>]]>";
    constexpr int MAX_EAGAIN_POLL_SLICE_MS = 1000;

    short libssh2_poll_events(LIBSSH2_SESSION* sess) {
        int directions = libssh2_session_block_directions(sess);
        short events = 0;

        if (directions & LIBSSH2_SESSION_BLOCK_INBOUND) {
            events |= POLLIN;
        }
        if (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
            events |= POLLOUT;
        }

        // libssh2 can occasionally return no direction even after EAGAIN.
        // Waiting for either side avoids a tight retry loop.
        if (events == 0) {
            events = POLLIN | POLLOUT;
        }

        return events;
    }

    int remaining_poll_timeout_ms(
        std::chrono::steady_clock::time_point deadline,
        bool infinite_wait
    ) {
        if (infinite_wait) {
            return MAX_EAGAIN_POLL_SLICE_MS;
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return 0;
        }

        auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now
        ).count();

        if (remaining_ms <= 0) {
            return 0;
        }

        return static_cast<int>(
            std::min<long long>(remaining_ms, MAX_EAGAIN_POLL_SLICE_MS)
        );
    }

    void wait_for_libssh2_socket(
        LIBSSH2_SESSION* sess,
        int soc_fd,
        std::chrono::steady_clock::time_point deadline,
        bool infinite_wait,
        int read_timeout
    ) {
        if (soc_fd < 0) {
            throw NetconfException("Invalid socket while waiting for channel readiness");
        }

        while (true) {
            int poll_timeout_ms = remaining_poll_timeout_ms(deadline, infinite_wait);
            if (!infinite_wait && poll_timeout_ms <= 0) {
                throw NetconfException(
                    "Device failed to send data within " +
                    std::to_string(read_timeout) +
                    "s, try increasing read_timeout"
                );
            }

            struct pollfd pfd{};
            pfd.fd = soc_fd;
            pfd.events = libssh2_poll_events(sess);

            int poll_ret = poll(&pfd, 1, poll_timeout_ms);
            if (poll_ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw NetconfException(
                    "Poll error while waiting for channel readiness: " +
                    std::string(strerror(errno))
                );
            }

            if (poll_ret == 0) {
                // if (infinite_wait) {
                //     continue;
                // }
                // I AM NOT CONTINUING HERE ANYMORE, I AM RETURNING.
                return;
            }

            if (pfd.revents & POLLNVAL) {
                throw NetconfException("Invalid socket while waiting for channel readiness");
            }
            if (pfd.revents & (POLLERR | POLLHUP)) {
                throw NetconfException("Socket closed or errored while waiting for channel readiness");
            }
            if (pfd.revents & (POLLIN | POLLOUT | POLLPRI)) {
                return;
            }

            return;
        }
    }
}

// ----------------------- Read Helpers -------------------------

std::string NetconfClient::read_until_eom_non_blocking(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess,
    int soc_fd,
    int read_timeout,
    int notif_incomplete_max_kb,
    int notif_incomplete_timeout
) {
    std::string response;
    char buffer[1024];

    const bool infinite_wait = (read_timeout < 0);
    const auto timeout = std::chrono::seconds(infinite_wait ? 0 : read_timeout);

    const bool incomplete_size_limit_enabled =
        infinite_wait && notif_incomplete_max_kb > 0;

    const bool incomplete_time_limit_enabled =
        infinite_wait && notif_incomplete_timeout > 0;

    const bool incomplete_guard_enabled =
        incomplete_size_limit_enabled || incomplete_time_limit_enabled;

    const size_t incomplete_max_bytes =
        incomplete_size_limit_enabled
            ? static_cast<size_t>(notif_incomplete_max_kb) * 1024
            : 0;

    auto last_data_time = std::chrono::steady_clock::now();
    auto first_data_time = last_data_time;

    bool got_any_data = false;
    bool returning_incomplete = false;

    try {
        while (true) {
            if (!chan || !sess) {
                throw NetconfException("Operation cancelled: connection object is missing");
            }

            auto now = std::chrono::steady_clock::now();

            if (incomplete_guard_enabled && got_any_data) {
                if (incomplete_time_limit_enabled &&
                    now - first_data_time >= std::chrono::seconds(notif_incomplete_timeout)) {
                    returning_incomplete = true;
                    break;
                }

                if (incomplete_size_limit_enabled &&
                    response.size() >= incomplete_max_bytes) {
                    returning_incomplete = true;
                    break;
                }
            }

            auto deadline = last_data_time + timeout;

            if (!infinite_wait && now >= deadline) {
                throw NetconfException(
                    "Device failed to send data within " +
                    std::to_string(read_timeout) +
                    "s, try increasing read_timeout"
                );
            }

            int nbytes = libssh2_channel_read_nonblocking(
                chan,
                buffer,
                sizeof(buffer),
                0
            );

            if (nbytes == LIBSSH2_ERROR_EAGAIN) {
                if (infinite_wait && !got_any_data) {
                    return response;
                }

                wait_for_libssh2_socket(
                    sess,
                    soc_fd,
                    deadline,
                    infinite_wait,
                    read_timeout
                );
                continue;
            }

            if (nbytes < 0) {
                char* err_msg = nullptr;
                libssh2_session_last_error(sess, &err_msg, nullptr, 0);
                throw NetconfException(
                    "Error reading from channel: " +
                    std::string(err_msg ? err_msg : "Unknown error")
                );
            }

            if (nbytes == 0) {
                if (infinite_wait && !got_any_data) {
                    return response;
                }

                wait_for_libssh2_socket(
                    sess,
                    soc_fd,
                    deadline,
                    infinite_wait,
                    read_timeout
                );
                continue;
            }

            if (!got_any_data) {
                got_any_data = true;
                first_data_time = std::chrono::steady_clock::now();
            }

            response.append(buffer, nbytes);
            last_data_time = std::chrono::steady_clock::now();

            if (response.find(NETCONF_EOM) != std::string::npos) {
                break;
            }

            if (incomplete_size_limit_enabled &&
                response.size() >= incomplete_max_bytes) {
                returning_incomplete = true;
                break;
            }
        }

        if (returning_incomplete) {
            std::cerr
                << "Received incomplete NETCONF notification. "
                << "Returning partial notification after receiving "
                << response.size()
                << " bytes without NETCONF EOM."
                << std::endl;
        }

    } catch (const std::exception& e) {
        throw NetconfException(
            "Error occured while reading from channel: " + std::string(e.what())
        );
    }

    return response;
}


std::string NetconfClient::read_until_eom_blocking(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess,
    int read_timeout
) {
    std::string response;
    std::string tail;
    auto last_data_time = std::chrono::steady_clock::now();
    
    // Determine whether we should ever timeout:
    const bool infinite_wait = (read_timeout < 0);
    char buffer[2048];

    // If not infinite, prepare a std::chrono timeout duration
    const std::chrono::seconds timeout{ infinite_wait ? 0 : read_timeout };
    try {
        while (true) {
            if (!chan) {
                throw NetconfException("Operation cancelled: connection object is missing");
            }

            // Only check elapsed time if we're NOT in infinite-wait mode
            if (!infinite_wait &&
                std::chrono::steady_clock::now() - last_data_time > timeout)
            {
                throw NetconfException(
                    "Device failed to send data within " +
                    std::to_string(read_timeout) +
                    "s, try increasing read_timeout"
                );
            }

            int nbytes = libssh2_channel_read(chan, buffer, sizeof(buffer));
            if (nbytes == LIBSSH2_ERROR_EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (nbytes < 0) {
                char* err_msg = nullptr;
                libssh2_session_last_error(sess, &err_msg, nullptr, 0);
                throw NetconfException(
                    "Error reading from channel: " +
                    std::string(err_msg ? err_msg : "Unknown error")
                );
            }
            // nbytes > 0
            response.append(buffer, nbytes);
            std::string new_data(buffer, nbytes);

            // keep last 7 chars from previous plus the new data to search for end-marker
            if (response.size() >= 7) {
                tail = response.substr(response.size() - 7);
            } else {
                tail = response;
            }
            if ((tail + new_data).find("]]>]]>") != std::string::npos) {
                break;
            }

            // we got some real data, reset our timeout clock
            last_data_time = std::chrono::steady_clock::now();
        }
    } catch (const std::exception& e) {
        throw NetconfException("Error occured while reading from channel: " + std::string(e.what()));
    }
    return response;
}

// ----------------------- Build & Send Helpers -------------------------

std::string NetconfClient::build_client_hello() {
    return
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<hello xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">)"
          R"(<capabilities>)"
            R"(<capability>urn:ietf:params:netconf:base:1.0</capability>)"
          R"(</capabilities>)"
        R"(</hello>)"
        "]]>]]>";
}

void NetconfClient::send_client_hello_non_blocking(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess,
    int sock_fd
) {
    std::string hello = build_client_hello();
    size_t total_written = 0;
    size_t data_length = hello.size();

    while (total_written < data_length) {
        try {
            int rc = libssh2_channel_write(chan, hello.data() + total_written, data_length - total_written);
            if (rc == LIBSSH2_ERROR_EAGAIN) {
                // Channel is not ready for writing.
                struct pollfd pfd;
                int fd = sock_fd;
                pfd.fd = fd;
                pfd.events = POLLOUT;
                int poll_ret = poll(&pfd, 1, 1000); // wait up to 1000 ms
                if (poll_ret < 0) {
                    throw NetconfException("Poll error during send_client_hello: " + std::string(strerror(errno)));
                }
                // If poll_ret is 0 (timeout), we simply try again.
                continue;
            } else if (rc < 0) {
                char* err_msg = nullptr;
                libssh2_session_last_error(sess, &err_msg, nullptr, 0);
                throw NetconfException("Failed to send client <hello>: " +
                                    std::string(err_msg ? err_msg : "Unknown error"));
            } else {
                total_written += rc;
            }
        } catch (const std::exception& e) {
            throw NetconfException("Error occured sending hello message: " + std::string(e.what()));
        }
    }
}


void NetconfClient::send_client_hello_blocking(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess
) {
    try {
        std::string hello = build_client_hello();
        int rc = libssh2_channel_write(chan, hello.c_str(), hello.size());
        if (rc < 0) {
            char* err_msg = nullptr;
            libssh2_session_last_error(sess, &err_msg, nullptr, 0);
            throw NetconfException("Failed to send client <hello>: " +
                                std::string(err_msg ? err_msg : "Unknown error"));
        }
    } catch (const std::exception& e) {
        throw NetconfException("Error occured while sending hello message: " + std::string(e.what()));
    }
}

std::string NetconfClient::send_rpc_blocking_func(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess,
    const std::string& rpc,
    int read_timeout
) {
    try {
        if (!chan) {
            throw NetconfException("Channel not open.");
        }
        std::string rpc_with_eom = rpc + "\n]]>]]>\n";
        int rc = libssh2_channel_write(chan, rpc_with_eom.c_str(), rpc_with_eom.size());
        if (rc < 0) {
            char* err_msg = nullptr;
            libssh2_session_last_error(sess, &err_msg, nullptr, 0);
            throw NetconfException("Failed to send RPC: " +
                                std::string(err_msg ? err_msg : "Unknown error"));
        }
        std::string reply = read_until_eom_blocking(chan, sess, read_timeout);
        check_for_rpc_error(reply);
        return reply;
    } catch (const std::exception& e) {
        throw NetconfException("Error occured while sending RPC: " + std::string(e.what()));
    }
}

std::string NetconfClient::send_rpc_non_blocking_func(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess,
    int soc_fd,
    const std::string& rpc,
    int read_timeout
) {
        try {
            if (!chan) {
                throw NetconfException("Channel not open.");
            }
            // Append the end-of-message delimiter.
            std::string rpc_with_eom = rpc + "\n]]>]]>\n";
            size_t total_written = 0;
            size_t data_length = rpc_with_eom.size();

            // Write the entire message in a nonblocking loop.
            while (total_written < data_length) {
                int rc = libssh2_channel_write(chan,
                                            rpc_with_eom.data() + total_written,
                                            data_length - total_written);
                if (rc == LIBSSH2_ERROR_EAGAIN) {
                    // If the channel cannot accept more data right now, poll for writability.
                    struct pollfd pfd;
                    int fd = soc_fd;
                    pfd.fd = fd;
                    pfd.events = POLLOUT;
                    int poll_ret = poll(
                        &pfd,
                        1,
                        500
                    );
                    if (poll_ret < 0) {
                        throw NetconfException("Poll error during write: " + std::string(strerror(errno)));
                    }
                    // If poll_ret == 0, the channel still isn’t ready; loop and try again.
                    continue;
                } else if (rc < 0) {
                    char* err_msg = nullptr;
                    libssh2_session_last_error(sess, &err_msg, nullptr, 0);
                    throw NetconfException("Failed to send RPC: " +
                                        std::string(err_msg ? err_msg : "Unknown error"));
                } else {
                    total_written += rc;
                }
            }
            // Once the entire RPC message is written, read the reply.
            std::string reply = read_until_eom_non_blocking(chan, sess, soc_fd, read_timeout);
            check_for_rpc_error(reply);
            return reply;
        } catch (const std::exception& e) {
            throw NetconfException("Error occured while sending RPC: " + std::string(e.what()));
        }
}
