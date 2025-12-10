#include "ThreadPool.h"

ThreadPool::ThreadPool() = default;
ThreadPool::~ThreadPool() { stop(); }

void ThreadPool::start(size_t n) {
    stop();
    stopping = false;
    workers.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        workers.emplace_back([this]{
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lk(m);
                    cv.wait(lk, [this]{ return stopping || !tasks.empty(); });
                    if (stopping && tasks.empty()) break;
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();
            }
        });
    }
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(m);
        tasks.push(std::move(task));
    }
    cv.notify_one();
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lk(m);
        stopping = true;
    }
    cv.notify_all();
    for (auto &t : workers) if (t.joinable()) t.join();
    workers.clear();
    {
        std::lock_guard<std::mutex> lk(m);
        std::queue<std::function<void()>> empty; tasks.swap(empty);
    }
}

