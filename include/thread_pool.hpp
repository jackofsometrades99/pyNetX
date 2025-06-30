// ThreadPool.hpp
#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>
#include <stdexcept>

class ThreadPool {
public:
    explicit ThreadPool(size_t nThreads)
    : stop_{false}
    {
    workers_.reserve(nThreads);
    for (size_t i = 0; i < nThreads; ++i) {
        // actually create a Worker
        workers_.emplace_back(std::make_unique<Worker>());
        // then start its thread
        threads_.emplace_back([this, i] {
        auto &worker = *workers_[i];
        for (;;) {
            std::function<void()> task;
            {
            std::unique_lock<std::mutex> lock(worker.mtx);
            worker.cv.wait(lock, [&]{
                return stop_.load() || !worker.tasks.empty();
            });
            if (stop_.load() && worker.tasks.empty())
                return;
            task = std::move(worker.tasks.front());
            worker.tasks.pop();
            }
            worker.inflight.fetch_sub(1, std::memory_order_relaxed);
            task();
        }
        });
    }
    }

    ~ThreadPool() {
        stop_.store(true);
        // wake up all threads
        for (auto &w : workers_) w->cv.notify_all();
        for (auto &t : threads_)
            if (t.joinable()) t.join();
    }

    // enqueue: pick the worker with the smallest inflight count
    template<class F>
    auto enqueue(F&& f)
      -> std::future<typename std::result_of<F()>::type>
    {
        using Ret = typename std::result_of<F()>::type;
        auto taskPtr = std::make_shared<std::packaged_task<Ret()>>(std::forward<F>(f));
        std::future<Ret> fut = taskPtr->get_future();

        // pick least-loaded queue
        size_t best = 0;
        size_t bestCount = SIZE_MAX;
        for (size_t i = 0; i < workers_.size(); ++i) {
            size_t cnt = workers_[i]->inflight.load(std::memory_order_relaxed);
            if (cnt < bestCount) {
                bestCount = cnt;
                best = i;
            }
        }

        auto &worker = *workers_[best];
        {
            std::lock_guard<std::mutex> lock(worker.mtx);
            if (stop_.load()) {
                throw std::runtime_error("ThreadPool is stopped");
            }
            worker.tasks.emplace([taskPtr](){ (*taskPtr)(); });
            // increment to account for this new task
            worker.inflight.fetch_add(1, std::memory_order_relaxed);
        }
        worker.cv.notify_one();
        return fut;
    }

private:
  struct Worker {
    std::queue<std::function<void()>> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<size_t> inflight{0};
  };

  std::vector<std::unique_ptr<Worker>> workers_;
  std::vector<std::thread> threads_;
  std::atomic<bool> stop_;
};

#endif // THREAD_POOL_HPP
