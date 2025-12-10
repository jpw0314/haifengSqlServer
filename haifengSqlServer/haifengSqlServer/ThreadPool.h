#pragma once
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();
    void start(size_t n);
    void submit(std::function<void()> task);
    void stop();
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex m;
    std::condition_variable cv;
    bool stopping = false;
};

