#pragma once
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

// 简单的线程池实现，用于管理工作线程和任务队列
class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();

    // 启动线程池
    // n: 工作线程数量
    void start(size_t n);

    // 提交任务到线程池
    // task: 要执行的任务函数
    void submit(std::function<void()> task);

    // 停止线程池并等待所有工作线程结束
    void stop();
private:
    std::vector<std::thread> workers;        // 工作线程集合
    std::queue<std::function<void()>> tasks; // 任务队列
    std::mutex m;                            // 互斥锁，保护任务队列
    std::condition_variable cv;              // 条件变量，用于线程同步
    bool stopping = false;                   // 停止标志
};

