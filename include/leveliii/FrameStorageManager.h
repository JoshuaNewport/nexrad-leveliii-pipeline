#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <chrono>
#include <shared_mutex>
#include <queue>
#include <list>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "LevelIII_Types.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace leveliii {

constexpr size_t MAX_INDEX_CACHE_SIZE = 64;

struct AsyncWriteTask {
    enum Type {
        BITMASK,
        VOLUMETRIC_BITMASK
    } type;
    
    std::string station;
    int16_t product_code;
    std::string product_name;
    std::string timestamp;
    float elevation;
    std::vector<uint8_t> bitmask;
    std::vector<uint8_t> values;
    std::vector<float> elevations;
    uint16_t num_rays;
    uint16_t num_gates;
    float gate_spacing;
    float first_gate;
};

class FrameStorageManager {
public:
    struct FrameMetadata {
        std::string station;
        int16_t product_code;
        std::string product_name;
        std::string timestamp;
        float elevation;
        size_t file_size;
        std::chrono::system_clock::time_point stored_time;
        std::string file_path;
    };

    struct CompressedFrameData {
        json metadata;
        std::vector<uint8_t> binary_data;
    };

    explicit FrameStorageManager(const std::string& base_path = "./data/leveliii");
    
    ~FrameStorageManager();
    
    void enqueue_async_write(AsyncWriteTask&& task);
    
    void shutdown_async_storage();

    bool save_frame_bitmask(
        const std::string& station,
        int16_t product_code,
        const std::string& product_name,
        const std::string& timestamp,
        float elevation,
        uint16_t num_rays,
        uint16_t num_gates,
        float gate_spacing,
        float first_gate,
        const std::vector<uint8_t>& bitmask,
        const std::vector<uint8_t>& values,
        bool auto_update_index = true
    );
    
    bool save_volumetric_bitmask(
        const std::string& station,
        int16_t product_code,
        const std::string& product_name,
        const std::string& timestamp,
        const std::vector<float>& elevations,
        uint16_t num_rays,
        uint16_t num_gates,
        float gate_spacing,
        float first_gate,
        const std::vector<uint8_t>& bitmask,
        const std::vector<uint8_t>& values,
        bool auto_update_index = true
    );

    bool load_frame_bitmask(
        const std::string& station,
        const std::string& product_name,
        const std::string& timestamp,
        float elevation,
        CompressedFrameData& out_data
    ) const;

    void update_index(const std::string& station, const std::string& storage_category);
    json get_index(const std::string& station, const std::string& storage_category) const;
    
    bool has_timestamp_product(
        const std::string& station,
        const std::string& product_name,
        const std::string& timestamp
    ) const;
    
    void cleanup_old_frames(int max_frames_per_station = 30);
    
    std::string get_base_path() const;
    void set_base_path(const std::string& path);
    
    size_t get_total_disk_usage() const;
    int get_frame_count() const;
    size_t num_pending_tasks() const {
        std::lock_guard<std::mutex> lock(write_queue_mutex_);
        return write_queue_.size();
    }
    
private:
    std::string base_path_;
    mutable std::mutex base_path_mutex_;
    mutable std::shared_mutex index_mutex_;
    mutable std::unordered_map<std::string, json> index_cache_;
    mutable std::list<std::string> index_lru_list_;
    mutable std::unordered_map<std::string, std::list<std::string>::iterator> index_lru_map_;

    mutable std::mutex stats_mutex_;
    std::atomic<size_t> total_disk_usage_{0};
    std::atomic<int> total_frame_count_{0};
    
    std::queue<AsyncWriteTask> write_queue_;
    mutable std::mutex write_queue_mutex_;
    std::condition_variable write_queue_cv_;
    std::condition_variable write_queue_full_cv_;
    std::thread storage_thread_;
    std::atomic<bool> async_storage_running_{false};
    std::atomic<bool> async_storage_stop_{false};
    
    const size_t MAX_WRITE_QUEUE_SIZE = 500;
    
    void async_storage_loop();
    void process_write_task(const AsyncWriteTask& task);
    
    bool ensure_directory_exists(const std::string& path) const;
    std::string get_frame_path(
        const std::string& station,
        int16_t product_code,
        const std::string& product_name,
        const std::string& timestamp,
        float elevation
    ) const;
    
    std::string get_index_path(
        const std::string& station,
        const std::string& storage_category
    ) const;
    
    std::vector<FrameMetadata> scan_directory(
        const std::string& station,
        const std::string& storage_category
    ) const;
};

} // namespace leveliii
