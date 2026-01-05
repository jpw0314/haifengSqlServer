#include "ThreadPool.h"

ThreadPool::ThreadPool() = default;
ThreadPool::~ThreadPool() { stop(); }

// 启动指定数量的工作线程
void ThreadPool::start(size_t n) {
    stop(); // 确保之前的线程已停止
    stopping = false;
    workers.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        workers.emplace_back([this]{
            for (;;) {
                std::function<void()> task;
                {
                    // 获取锁并等待任务或停止信号
                    std::unique_lock<std::mutex> lk(m);
                    cv.wait(lk, [this]{ return stopping || !tasks.empty(); });
                    // 如果收到停止信号且任务队列为空，则退出线程
                    if (stopping && tasks.empty()) break;
                    // 取出任务
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                // 执行任务
                task();
            }
        });
    }
}

// 提交任务到队列并通知工作线程
void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(m);
        tasks.push(std::move(task));
    }
    cv.notify_one();
}

// 停止线程池
void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lk(m);
        stopping = true;
    }
    cv.notify_all(); // 唤醒所有线程以便它们能检查停止标志
    for (auto &t : workers) if (t.joinable()) t.join();
    workers.clear();
    {
        std::lock_guard<std::mutex> lk(m);
        std::queue<std::function<void()>> empty; tasks.swap(empty); // 清空任务队列
    }
}

