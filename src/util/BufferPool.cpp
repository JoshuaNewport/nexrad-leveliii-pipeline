#include "leveliii/BufferPool.h"

namespace leveliii {

BufferPool::BufferPool(size_t num_buffers, size_t buffer_size) : buffer_size_(buffer_size) {
    for (size_t i = 0; i < num_buffers; ++i) {
        auto buf = std::make_unique<std::vector<uint8_t>>();
        buf->reserve(buffer_size_);
        available_.push(buf.get());
        buffers_.push_back(std::move(buf));
    }
}

std::vector<uint8_t>* BufferPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !available_.empty() || stop_; });
    
    if (stop_ && available_.empty()) {
        return nullptr;
    }
    
    auto* buf = available_.front();
    available_.pop();
    in_use_.insert(buf);
    return buf;
}

void BufferPool::release(std::vector<uint8_t>* buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!buffer) return;
    
    if (in_use_.find(buffer) == in_use_.end()) {
        return;
    }
    
    in_use_.erase(buffer);
    
    if (buffer->capacity() > buffer_size_ * 2) {
        std::vector<uint8_t>().swap(*buffer);
        buffer->reserve(buffer_size_);
    } else {
        buffer->clear();
    }
    
    if (!stop_) {
        available_.push(buffer);
        cv_.notify_one();
    }
}

void BufferPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
}

} // namespace leveliii
