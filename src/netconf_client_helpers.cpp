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
    const std::string &hostname) {
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
}

std::string NetconfClient::resolve_hostname_non_blocking(
    const std::string &hostname,
    int timeout_seconds) {
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
}

// ----------------------- Read Helpers -------------------------


std::string NetconfClient::read_until_eom_non_blocking(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess,
    int read_timeout) {
        std::string response;
        std::string tail;
        char buffer[1024];
        auto last_data_time = std::chrono::steady_clock::now();
        const bool infinite_wait = (read_timeout < 0);
        const auto timeout = std::chrono::seconds(infinite_wait ? 0 : read_timeout);

        while (true) {
            if (!chan) {
                throw NetconfException("Operation cancelled: connection object is missing");
            }
            if (!infinite_wait && std::chrono::steady_clock::now() - last_data_time > timeout) {
                throw NetconfException("Device failed to send data , try increasing read_timeout");
            }
            int nbytes = libssh2_channel_read_nonblocking(chan, buffer, sizeof(buffer), 0);
            if (!infinite_wait){

                if (nbytes == LIBSSH2_ERROR_EAGAIN) {
                    std::this_thread::yield();
                    continue;
                } else if (nbytes < 0) {
                    char* err_msg = nullptr;
                    libssh2_session_last_error(sess, &err_msg, nullptr, 0);
                    throw NetconfException("Error reading from channel: " +
                                            std::string(err_msg ? err_msg : "Unknown error"));
                } else if (nbytes > 0) {
                    response.append(buffer, nbytes);
                    std::string new_data(buffer, nbytes);
                    if (response.size() >= 7) {
                        tail = response.substr(response.size() - 7, 7);
                    } else {
                        tail = response;
                    }
                    std::string check_str = tail + new_data;
                    if (check_str.find("]]>]]>") != std::string::npos) {
                        break;
                    }
                    last_data_time = std::chrono::steady_clock::now();
                    continue;
                }
            } else {
                if (nbytes == LIBSSH2_ERROR_EAGAIN) {
                    break;
                } else if (nbytes < 0) {
                    char* err_msg = nullptr;
                    libssh2_session_last_error(sess, &err_msg, nullptr, 0);
                    throw NetconfException("Error reading from channel: " +
                                            std::string(err_msg ? err_msg : "Unknown error"));
                } else if (nbytes > 0) {
                    response.append(buffer, nbytes);
                    continue;
                }
            }
        }
        return response;
}


std::string NetconfClient::read_until_eom_blocking(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess,
    int read_timeout)
{
    std::string response;
    std::string tail;
    auto last_data_time = std::chrono::steady_clock::now();
    
    // Determine whether we should ever timeout:
    const bool infinite_wait = (read_timeout < 0);
    char buffer[2048];

    // If not infinite, prepare a std::chrono timeout duration
    const std::chrono::seconds timeout{ infinite_wait ? 0 : read_timeout };

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
) 
{
    std::string hello = build_client_hello();
    size_t total_written = 0;
    size_t data_length = hello.size();

    while (total_written < data_length) {
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
    }
}


void NetconfClient::send_client_hello_blocking(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess) {
        std::string hello = build_client_hello();
        int rc = libssh2_channel_write(chan, hello.c_str(), hello.size());
        if (rc < 0) {
            char* err_msg = nullptr;
            libssh2_session_last_error(sess, &err_msg, nullptr, 0);
            throw NetconfException("Failed to send client <hello>: " +
                                std::string(err_msg ? err_msg : "Unknown error"));
        }
}

std::string NetconfClient::send_rpc_blocking_func(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess,
    const std::string& rpc,
    int read_timeout) {
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
}

std::string NetconfClient::send_rpc_non_blocking_func(
    LIBSSH2_CHANNEL *chan,
    LIBSSH2_SESSION *sess,
    int soc_fd,
    const std::string& rpc,
    int read_timeout) {
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
        std::string reply = read_until_eom_non_blocking(chan, sess, read_timeout);
        check_for_rpc_error(reply);
        return reply;
}
