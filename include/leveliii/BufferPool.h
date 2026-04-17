#pragma once

#include <vector>
#include <queue>
#include <set>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <iostream>

namespace leveliii {

class BufferPool {
public:
    explicit BufferPool(size_t num_buffers = 8, size_t buffer_size = 1024 * 1024);
    ~BufferPool() = default;

    std::vector<uint8_t>* acquire();
    void release(std::vector<uint8_t>* buffer);
    void shutdown();

private:
    size_t buffer_size_;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> buffers_;
    std::queue<std::vector<uint8_t>*> available_;
    std::set<std::vector<uint8_t>*> in_use_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> logging_enabled_{false};

    friend class ScopedBuffer;
};

class ScopedBuffer {
public:
    explicit ScopedBuffer(std::shared_ptr<BufferPool> pool) : pool_(std::move(pool)), buffer_(nullptr) {
        if (pool_) {
            buffer_ = pool_->acquire();
        }
    }

    ~ScopedBuffer() {
        if (pool_ && buffer_) {
            pool_->release(buffer_);
        }
    }

    ScopedBuffer(const ScopedBuffer&) = delete;
    ScopedBuffer& operator=(const ScopedBuffer&) = delete;

    ScopedBuffer(ScopedBuffer&& other) noexcept : pool_(std::move(other.pool_)), buffer_(other.buffer_) {
        other.buffer_ = nullptr;
    }

    ScopedBuffer& operator=(ScopedBuffer&& other) noexcept {
        if (this != &other) {
            if (pool_ && buffer_) {
                pool_->release(buffer_);
            }
            pool_ = std::move(other.pool_);
            buffer_ = other.buffer_;
            other.buffer_ = nullptr;
        }
        return *this;
    }

    bool valid() const { return buffer_ != nullptr; }
    std::vector<uint8_t>* get() { return buffer_; }
    std::vector<uint8_t>& operator*() { return *buffer_; }
    std::vector<uint8_t>* operator->() { return buffer_; }

    void reset() {
        if (pool_ && buffer_) {
            pool_->release(buffer_);
            buffer_ = nullptr;
        }
    }

private:
    std::shared_ptr<BufferPool> pool_;
    std::vector<uint8_t>* buffer_;
};

} // namespace leveliii
