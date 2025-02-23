#include "thread_pool_global.hpp"
#include "thread_pool.hpp"
#include <memory>
#include <stdexcept>
#include <thread>
#include <iostream>

static std::unique_ptr<ThreadPool> gThreadPool;

void init_global_pool(int nThreads) {
    if (nThreads <= 0) {
        throw std::runtime_error("Invalid thread pool size");
    }
    gThreadPool = std::make_unique<ThreadPool>(nThreads);
}

ThreadPool& get_pool() {
    if (!gThreadPool) {
        init_global_pool(4);
    }
    return *gThreadPool;
}
