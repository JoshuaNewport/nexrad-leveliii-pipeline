#pragma once

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <vector>
#include <atomic>

namespace leveliii {

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t worker_count = 0, size_t max_queue_size = 0);

    ~ThreadPool();

    void enqueue(Task task);

    void shutdown();

    bool is_running() const { return !stop_.load(); }

    size_t worker_count() const { return workers_.size(); }
    size_t active_threads() const { return active_threads_.load(); }
    size_t pending_tasks() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return task_queue_.size();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<Task> task_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable full_cv_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_threads_{0};
    size_t max_queue_size_ = 0;

    void worker_loop();
};

} // namespace leveliii
