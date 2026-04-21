#pragma once

#include <string>
#include <vector>
#include <set>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>
#include <queue>
#include <map>
#include <list>
#include <nlohmann/json.hpp>
#include "leveliii/ThreadPool.h"
#include "leveliii/BufferPool.h"
#include <aws/s3/S3Client.h>
#include <condition_variable>
#include <chrono>

using json = nlohmann::json;

namespace leveliii {

class FrameStorageManager;

struct DiscoveryItem {
    std::string station;
    std::string product;
    std::string key;
    std::chrono::system_clock::time_point last_modified;
};

struct FrameFetcherConfig {
    std::set<std::string> monitored_stations = {"KTLX", "KCRP", "KEWX"};
    std::vector<std::string> products = {};

    int scan_interval_seconds = 10;
    int max_frames_per_station = 100;
    
    int thread_pool_size = 16;
    int max_concurrent_discoveries = 32;
    int discovery_catchup_limit = 200;
    int discovery_queue_threshold = 500;
    int data_lookback_hours = 2;
    
    bool catchup = false;
};

class BackgroundFrameFetcher {
public:
    struct ObjectRecord {
        std::string key;
        std::chrono::system_clock::time_point last_modified;
    };

    explicit BackgroundFrameFetcher(
        std::shared_ptr<FrameStorageManager> storage,
        const FrameFetcherConfig& config
    );

    ~BackgroundFrameFetcher();

    void start();
    void stop();
    bool is_running() const { return is_running_; }

    void set_monitored_stations(const std::set<std::string>& stations);
    std::set<std::string> get_monitored_stations() const;

    json get_statistics() const;
    
    FrameFetcherConfig get_config() const;
    void set_config(const FrameFetcherConfig& config);
    
    void save_config() const;
    void load_config();

private:
    std::shared_ptr<FrameStorageManager> storage_;
    FrameFetcherConfig config_;
    std::shared_ptr<ThreadPool> thread_pool_;
    std::shared_ptr<BufferPool> buffer_pool_;
    std::unique_ptr<Aws::S3::S3Client> s3_client_;
    std::string base_path_;

    std::thread discovery_thread_;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> should_stop_{false};

    mutable std::mutex config_mutex_;
    
    // Cache of objects discovered (exactly like scwx)
    // station_product -> Map of TimePoint -> ObjectRecord
    std::map<std::string, std::map<std::chrono::system_clock::time_point, ObjectRecord>> objects_;
    std::map<std::string, std::set<std::chrono::system_clock::time_point>> object_dates_;
    std::map<std::string, std::chrono::system_clock::time_point> refresh_date_;
    mutable std::mutex objects_mutex_;

    // Stats
    std::atomic<uint64_t> frames_fetched_{0};
    std::atomic<uint64_t> frames_failed_{0};
    int cleanup_counter_{0};
    static constexpr int CLEANUP_INTERVAL = 10;
    std::map<std::string, std::string> last_processed_key_; // station_product -> last_key
    std::map<std::string, uint64_t> station_frame_counts_; // station -> frame count
    std::map<std::string, std::chrono::system_clock::time_point> station_last_fetch_time_; // station -> last fetch time
    mutable std::mutex stats_mutex_;

    void discovery_loop();
    std::pair<size_t, size_t> refresh();
    std::tuple<bool, size_t, size_t> list_objects(const std::string& station, const std::string& product, std::chrono::system_clock::time_point date);
    
    void fetch_and_process(const DiscoveryItem& item);
    std::string ExtractTimestampFromKey(const std::string& key);
    
    std::string get_prefix(const std::string& station, const std::string& product, std::chrono::system_clock::time_point date);
    std::chrono::system_clock::time_point get_time_point_from_key(const std::string& key);

    void log_info(const std::string& msg) const;
    void log_error(const std::string& msg) const;
};

} // namespace leveliii
