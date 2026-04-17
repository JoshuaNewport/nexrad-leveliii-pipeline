#include "leveliii/BackgroundFrameFetcher.h"
#include "LevelIII_Parser.h"
#include "leveliii/FrameStorageManager.h"
#include "leveliii/ProductDatabase.h"
#include "leveliii/BufferPool.h"
#include <aws/s3/S3Client.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <algorithm>

namespace leveliii {

static const char* NEXRAD_LEVEL3_BUCKET = "unidata-nexrad-level3";

static std::string get_s3_station_id(const std::string& radarId) {
    size_t siteIdIndex = std::max<size_t>(radarId.length(), 3u) - 3u;
    return radarId.substr(siteIdIndex);
}

BackgroundFrameFetcher::BackgroundFrameFetcher(
    std::shared_ptr<FrameStorageManager> storage,
    const FrameFetcherConfig& config)
    : storage_(storage), config_(config), base_path_("./") {
    
    Aws::Client::ClientConfiguration aws_config;
    aws_config.region = "us-east-1";
    aws_config.connectTimeoutMs = 10000;
    aws_config.requestTimeoutMs = 10000;
    
    auto anon_creds_provider = std::make_shared<Aws::Auth::AnonymousAWSCredentialsProvider>();
    
    s3_client_ = std::make_unique<Aws::S3::S3Client>(
        anon_creds_provider,
        aws_config,
        Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
        true
    );

    buffer_pool_ = std::make_shared<BufferPool>(16, 4 * 1024 * 1024);
    thread_pool_ = std::make_shared<ThreadPool>(config_.thread_pool_size);
    
    load_config();
}

BackgroundFrameFetcher::~BackgroundFrameFetcher() {
    stop();
}

void BackgroundFrameFetcher::start() {
    if (is_running_.load()) return;
    
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        if (config_.products.empty()) {
            config_.products = ProductDatabase::Instance().GetAllAwipsNames();
        }
    }
    
    should_stop_.store(false);
    is_running_.store(true);
    
    discovery_thread_ = std::thread([this]() { this->discovery_loop(); });
}

void BackgroundFrameFetcher::stop() {
    if (!is_running_.load()) return;
    
    should_stop_.store(true);
    is_running_.store(false);
    
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
    
    if (thread_pool_) thread_pool_->shutdown();
}

void BackgroundFrameFetcher::set_monitored_stations(const std::set<std::string>& stations) {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_.monitored_stations = stations;
    }
    save_config();
}

std::set<std::string> BackgroundFrameFetcher::get_monitored_stations() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_.monitored_stations;
}

void BackgroundFrameFetcher::discovery_loop() {
    log_info("Level III Discovery loop started");

    while (!should_stop_.load()) {
        size_t pending_tasks = thread_pool_ ? thread_pool_->pending_tasks() : 0;
        int threshold = 500;
        int interval = 10;
        bool latest_only = false;
        
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            threshold = config_.discovery_queue_threshold;
            interval = config_.scan_interval_seconds;
            latest_only = !config_.catchup;
        }
        
        // In latest_only mode (catchup=false), use much lower threshold (only 1 per station-product)
        if (latest_only) {
            threshold = 100;
        }
        
        if (pending_tasks < static_cast<size_t>(threshold)) {
            refresh();
        }

        for (int i = 0; i < interval * 10 && !should_stop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

std::pair<size_t, size_t> BackgroundFrameFetcher::refresh() {

    auto now = std::chrono::system_clock::now();

    // Convert to UTC calendar time
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utc = *std::gmtime(&t);

    // Compute today's UTC midnight
    utc.tm_hour = 0;
    utc.tm_min  = 0;
    utc.tm_sec  = 0;

    auto today_midnight =
        std::chrono::system_clock::from_time_t(timegm(&utc));

    auto yesterday_midnight = today_midnight - std::chrono::hours(24);
    
    // Only search for data from last 2 hours to avoid massive backlogs
    auto recent_cutoff = now - std::chrono::hours(2);

    bool within_midnight_grace =
        now < (today_midnight + std::chrono::minutes(10));

    std::set<std::string> stations;
    std::vector<std::string> products;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        stations = config_.monitored_stations;
        products = config_.products;
    }

    std::atomic<size_t> all_new(0);
    std::atomic<size_t> all_total(0);
    std::vector<std::future<void>> discovery_tasks;
    int max_concurrent = 0;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        max_concurrent = config_.max_concurrent_discoveries;
    }

    auto submit_discovery = [&](const std::string& station, const std::string& product) {
        discovery_tasks.push_back(
            std::async(std::launch::async, [this, station, product, today_midnight, yesterday_midnight, within_midnight_grace, &all_new, &all_total]() {
                std::string station_product = station + "_" + product;

                bool has_data = false;
                {
                    std::lock_guard<std::mutex> lock(objects_mutex_);
                    has_data = !objects_[station_product].empty();
                }

                auto [s2,n2,t2] = list_objects(station, product, today_midnight);
                all_new.fetch_add(n2);
                all_total.fetch_add(t2);

                if (within_midnight_grace) {
                    auto [s1,n1,t1] = list_objects(station, product, yesterday_midnight);
                    all_new.fetch_add(n1);
                    all_total.fetch_add(t1);
                }
            })
        );

        if (discovery_tasks.size() >= static_cast<size_t>(max_concurrent)) {
            discovery_tasks.front().wait();
            discovery_tasks.erase(discovery_tasks.begin());
        }
    };

    for (const auto& station : stations) {
        for (const auto& product : products) {
            if (should_stop_.load()) break;
            submit_discovery(station, product);
        }
    }

    for (auto& task : discovery_tasks) {
        if (task.valid()) {
            task.wait();
        }
    }

    return {all_new.load(), all_total.load()};
}

std::tuple<bool, size_t, size_t> BackgroundFrameFetcher::list_objects(
    const std::string& station, 
    const std::string& product, 
    std::chrono::system_clock::time_point date) {
    
    std::string prefix = get_prefix(station, product, date);
    log_info("Listing objects with prefix '" + prefix + "'");

    Aws::S3::Model::ListObjectsV2Request list_req;
    list_req.WithBucket(NEXRAD_LEVEL3_BUCKET).WithPrefix(prefix);
    
    auto list_outcome = s3_client_->ListObjectsV2(list_req);
    size_t new_objects = 0;
    size_t total_objects = 0;

    if (list_outcome.IsSuccess()) {
        auto objects = list_outcome.GetResult().GetContents();
        total_objects = objects.size();

        std::string station_product = station + "_" + product;
        std::vector<DiscoveryItem> to_process;
        
        bool latest_only = false;
        int lookback_hours = 2;
        int catchup_limit = 1000;
        {
            std::lock_guard<std::mutex> cfg_lock(config_mutex_);
            latest_only = !config_.catchup;
            lookback_hours = config_.data_lookback_hours;
            catchup_limit = config_.discovery_catchup_limit;
        }

        {
            std::lock_guard<std::mutex> lock(objects_mutex_);
            auto& product_cache = objects_[station_product];

            auto now = std::chrono::system_clock::now();
            auto cutoff_time = now - std::chrono::hours(lookback_hours);
            
            std::vector<DiscoveryItem> newly_discovered;
            std::chrono::system_clock::time_point latest_time;
            DiscoveryItem latest_item;
            bool found_latest = false;
            
            for (const auto& obj : objects) {
                std::string key = obj.GetKey();
                auto tp = get_time_point_from_key(key);
                
                if (tp < cutoff_time) {
                    continue;
                }
                
                if (product_cache.find(tp) == product_cache.end()) {
                    DiscoveryItem item;
                    item.station = station;
                    item.product = product;
                    item.key = key;
                    item.last_modified = std::chrono::system_clock::time_point(std::chrono::seconds(obj.GetLastModified().Seconds()));
                    
                    if (latest_only) {
                        if (!found_latest || tp > latest_time) {
                            latest_time = tp;
                            latest_item = item;
                            found_latest = true;
                        }
                    } else {
                        newly_discovered.push_back(item);
                    }
                }
            }
            
            if (latest_only && found_latest) {
                to_process.push_back(latest_item);
            } else if (!latest_only) {
                std::sort(newly_discovered.begin(), newly_discovered.end(), [](const DiscoveryItem& a, const DiscoveryItem& b) {
                    return a.key > b.key;
                });
                
                if (static_cast<int>(newly_discovered.size()) > catchup_limit) {
                    newly_discovered.erase(newly_discovered.begin() + catchup_limit, newly_discovered.end());
                }
                
                to_process = newly_discovered;
            }
            
            for (const auto& item : to_process) {
                auto tp = get_time_point_from_key(item.key);
                ObjectRecord record;
                record.key = item.key;
                record.last_modified = item.last_modified;
                product_cache[tp] = record;
                new_objects++;
            }
        }

        for (const auto& item : to_process) {
            thread_pool_->enqueue([this, item]() {
                this->fetch_and_process(item);
            });
        }

        return {true, new_objects, total_objects};
    } else {
        log_error("Failed to list objects for " + prefix + ": " + list_outcome.GetError().GetMessage());
        return {false, 0, 0};
    }
}

std::string BackgroundFrameFetcher::ExtractTimestampFromKey(const std::string& key) {
    // S3 key format: STATION_PRODUCT_YYYY_MM_DD_HH_MM_SS
    // Example: TLX_OHA_2026_04_15_22_56_48
    // Extract the last 19 characters (YYYY_MM_DD_HH_MM_SS) and convert to YYYYMMDD_HHMMSS
    if (key.length() >= 19) {
        std::string time_part = key.substr(key.length() - 19);
        // Convert from YYYY_MM_DD_HH_MM_SS to YYYYMMDD_HHMMSS
        std::string result;
        result += time_part.substr(0, 4);   // YYYY
        result += time_part.substr(5, 2);   // MM
        result += time_part.substr(8, 2);   // DD
        result += "_";
        result += time_part.substr(11, 2);  // HH
        result += time_part.substr(14, 2);  // MM
        result += time_part.substr(17, 2);  // SS
        return result;
    }
    return "";
}

void BackgroundFrameFetcher::fetch_and_process(const DiscoveryItem& item) {
    // Check if already processed
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        std::string station_product = item.station + "_" + item.product;
        if (last_processed_key_.count(station_product) > 0 && last_processed_key_[station_product] >= item.key) {
            return;
        }
    }

    log_info("Fetching " + item.key);
    
    Aws::S3::Model::GetObjectRequest get_req;
    get_req.WithBucket(NEXRAD_LEVEL3_BUCKET).WithKey(item.key);
    
    auto get_outcome = s3_client_->GetObject(get_req);
    if (get_outcome.IsSuccess()) {
        auto& body = get_outcome.GetResultWithOwnership().GetBody();
        
        ScopedBuffer raw_data(buffer_pool_);
        if (!raw_data.valid()) {
            frames_failed_.fetch_add(1);
            return;
        }
        raw_data->clear();
        
        const size_t CHUNK_SIZE = 64 * 1024;
        raw_data->reserve(CHUNK_SIZE * 20);
        char temp_buf[CHUNK_SIZE];
        while (body) {
            body.read(temp_buf, CHUNK_SIZE);
            std::streamsize bytes_read = body.gcount();
            if (bytes_read > 0) {
                if (raw_data->size() + bytes_read > raw_data->capacity()) {
                    raw_data->reserve(raw_data->capacity() * 2);
                }
                raw_data->insert(raw_data->end(), temp_buf, temp_buf + bytes_read);
            }
            if (!body) break;
        }
        
        if (raw_data->empty()) {
            frames_failed_.fetch_add(1);
            return;
        }
        
        std::stringstream data_stream;
        data_stream.write(reinterpret_cast<const char*>(raw_data->data()), raw_data->size());
        data_stream.seekg(0);
        
        Parser parser;
        if (parser.LoadData(data_stream)) {
            RadarFrame frame = parser.ExtractRadarFrame();
            
            if (frame.ray_count == 0 || frame.gate_count == 0) {
                log_error("Parsed frame for " + item.key + " has no data (rays: " + 
                          std::to_string(frame.ray_count) + ", gates: " + std::to_string(frame.gate_count) + ")");
                frames_failed_.fetch_add(1);
                
                // Mark as processed
                std::lock_guard<std::mutex> lock(stats_mutex_);
                std::string station_product = item.station + "_" + item.product;
                if (last_processed_key_[station_product] < item.key) {
                    last_processed_key_[station_product] = item.key;
                }
                return;
            }

            // Validate frame data integrity
            uint32_t total_bins = frame.ray_count * frame.gate_count;
            if (frame.data.size() < total_bins) {
                log_error("Parsed frame for " + item.key + " has incomplete data (" + 
                          std::to_string(frame.data.size()) + " < " + std::to_string(total_bins) + " expected)");
                frames_failed_.fetch_add(1);
                
                // Mark as processed
                std::lock_guard<std::mutex> lock(stats_mutex_);
                std::string station_product = item.station + "_" + item.product;
                if (last_processed_key_[station_product] < item.key) {
                    last_processed_key_[station_product] = item.key;
                }
                return;
            }

            // Adapt to FrameStorageManager's expectations
            AsyncWriteTask task;
            task.type = AsyncWriteTask::BITMASK;
            task.station = frame.station_id;
            if (task.station.empty()) task.station = item.station;
            
            task.product_code = frame.product_code;
            task.product_name = item.product;
            // Use timestamp from S3 key which has the correct time
            std::string s3_timestamp = ExtractTimestampFromKey(item.key);
            task.timestamp = s3_timestamp.empty() ? frame.timestamp : s3_timestamp;
            task.elevation = frame.elevation;
            task.num_rays = frame.ray_count;
            task.num_gates = frame.gate_count;
            task.gate_spacing = frame.gate_spacing;
            task.first_gate = frame.first_gate_dist;
            
            size_t bitmask_bytes = (total_bins + 7) / 8;
            task.bitmask.reserve(bitmask_bytes);
            task.bitmask.assign(bitmask_bytes, 0);
            task.values.reserve(total_bins / 4);
            
            static const uint8_t bit_table[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
            
            for (uint32_t i = 0; i < total_bins; ++i) {
                if (frame.data[i] > 0) {
                    task.bitmask[i >> 3] |= bit_table[i & 7];
                    task.values.push_back(frame.data[i]);
                }
            }
            
            storage_->enqueue_async_write(std::move(task));
            frames_fetched_.fetch_add(1);
            
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                std::string station_product = item.station + "_" + item.product;
                if (last_processed_key_[station_product] < item.key) {
                    last_processed_key_[station_product] = item.key;
                }
                station_frame_counts_[item.station]++;
                station_last_fetch_time_[item.station] = std::chrono::system_clock::now();
            }
            
            log_info("Successfully processed " + item.key + " (" + std::to_string(frame.ray_count) + " rays)");
        } else {
            log_error("Failed to parse data for " + item.key + " (LoadData failed)");
            frames_failed_.fetch_add(1);
            
            // Mark as processed
            std::lock_guard<std::mutex> lock(stats_mutex_);
            std::string station_product = item.station + "_" + item.product;
            if (last_processed_key_[station_product] < item.key) {
                last_processed_key_[station_product] = item.key;
            }
        }
    } else {
        log_error("Failed to fetch " + item.key + ": " + get_outcome.GetError().GetMessage());
        frames_failed_.fetch_add(1);
    }
}

std::string BackgroundFrameFetcher::get_prefix(
    const std::string& station, 
    const std::string& product, 
    std::chrono::system_clock::time_point date)
{
    std::string station_s3 = get_s3_station_id(station);

    if (date == std::chrono::system_clock::time_point{}) {
        // No date: historical tail
        return station_s3 + "_" + product + "_";
    }

    std::time_t t = std::chrono::system_clock::to_time_t(date);
    std::tm tm = *std::gmtime(&t);

    std::stringstream ss;
    ss << station_s3 << "_" << product << "_"
       << std::put_time(&tm, "%Y_%m_%d"); // Day prefix, ignores seconds
    return ss.str() + "_";
}

std::chrono::system_clock::time_point BackgroundFrameFetcher::get_time_point_from_key(const std::string& key) {
    // Filename format is GGG_PPP_YYYY_MM_DD_HH_MM_SS
    // e.g. TLX_N0R_2020_03_30_00_09_11
    
    // The prefix is GGG_PPP_ (8 bytes)
    constexpr size_t offset = 8;
    static const size_t formatSize = std::string("YYYY_MM_DD_HH_MM_SS").size();
    
    if (key.size() < offset + formatSize) {
        return std::chrono::system_clock::now();
    }

    std::string timeStr = key.substr(offset, formatSize);
    std::tm tm = {};
    // Manually parse YYYY_MM_DD_HH_MM_SS to avoid dependency on std::get_time or boost
    if (sscanf(timeStr.c_str(), "%d_%d_%d_%d_%d_%d", 
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday, 
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
        return std::chrono::system_clock::now();
    }
    
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

json BackgroundFrameFetcher::get_statistics() const {
    json stats;
    
    uint64_t fetched = frames_fetched_.load();
    uint64_t failed = frames_failed_.load();
    
    stats["is_running"] = is_running_.load();
    stats["frames_fetched"] = fetched;
    stats["frames_failed"] = failed;
    
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        stats["scan_interval"] = config_.scan_interval_seconds;
    }
    
    stats["last_fetch_timestamp"] = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    if (storage_) {
        stats["total_disk_usage_bytes"] = storage_->get_total_disk_usage();
        stats["frame_count"] = storage_->get_frame_count();
    }
    
    if (thread_pool_) {
        json tp_stats;
        tp_stats["worker_count"] = thread_pool_->worker_count();
        tp_stats["active_threads"] = thread_pool_->active_threads();
        tp_stats["pending_tasks"] = thread_pool_->pending_tasks();
        stats["thread_pool"] = tp_stats;
    }
    
    json discovery_pool;
    discovery_pool["worker_count"] = 1;
    discovery_pool["active_threads"] = is_running_.load() ? 1 : 0;
    discovery_pool["pending_tasks"] = 0;
    stats["discovery_pool"] = discovery_pool;
    
    json station_stats = json::object();
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        std::set<std::string> monitored;
        {
            std::lock_guard<std::mutex> cfg_lock(config_mutex_);
            monitored = config_.monitored_stations;
        }
        
        for (const auto& station : monitored) {
            json s_stat;
            
            auto count_it = station_frame_counts_.find(station);
            s_stat["frames_fetched"] = (count_it != station_frame_counts_.end()) ? count_it->second : 0UL;
            
            s_stat["last_frame_timestamp"] = "N/A";
            
            auto fetch_time_it = station_last_fetch_time_.find(station);
            if (fetch_time_it != station_last_fetch_time_.end()) {
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    fetch_time_it->second.time_since_epoch()
                ).count();
                s_stat["last_fetch_timestamp"] = static_cast<uint64_t>(ns);
            } else {
                s_stat["last_fetch_timestamp"] = 0UL;
            }
            
            s_stat["last_scan_timestamp"] = 0UL;
            
            auto it = last_processed_key_.find(station);
            if (it != last_processed_key_.end()) {
                s_stat["last_key"] = it->second;
            }
            
            station_stats[station] = s_stat;
        }
    }
    stats["station_stats"] = station_stats;
    
    return stats;
}

void BackgroundFrameFetcher::log_info(const std::string& msg) const {
    if (false) std::cout << "ℹ️  " << msg << std::endl;
}

void BackgroundFrameFetcher::log_error(const std::string& msg) const {
    std::cerr << "❌ " << msg << std::endl;
}

void BackgroundFrameFetcher::save_config() const {
    try {
        std::lock_guard<std::mutex> lock(config_mutex_);
        
        json config_json;
        json stations_array = json::array();
        for (const auto& station : config_.monitored_stations) {
            stations_array.push_back(station);
        }
        config_json["monitored_stations"] = stations_array;
        config_json["scan_interval_seconds"] = config_.scan_interval_seconds;
        config_json["max_frames_per_station"] = config_.max_frames_per_station;
        config_json["thread_pool_size"] = config_.thread_pool_size;
        
        std::string config_path = base_path_ + "/level3_config.json";
        std::ofstream config_file(config_path);
        if (config_file.is_open()) {
            config_file << config_json.dump(2);
            config_file.close();
        }
    } catch (const std::exception& e) {
        log_error("Failed to save config: " + std::string(e.what()));
    }
}

void BackgroundFrameFetcher::load_config() {
    try {
        std::string config_path = base_path_ + "/level3_config.json";
        std::ifstream config_file(config_path);
        
        if (!config_file.is_open()) {
            save_config();
            return;
        }
        
        json config_json;
        config_file >> config_json;
        config_file.close();
        
        std::lock_guard<std::mutex> lock(config_mutex_);
        
        if (config_json.contains("monitored_stations") && config_json["monitored_stations"].is_array()) {
            config_.monitored_stations.clear();
            for (const auto& station : config_json["monitored_stations"]) {
                if (station.is_string()) {
                    config_.monitored_stations.insert(station.get<std::string>());
                }
            }
        }
        
        if (config_json.contains("scan_interval_seconds")) {
            config_.scan_interval_seconds = config_json["scan_interval_seconds"];
        }
        
        if (config_json.contains("max_frames_per_station")) {
            config_.max_frames_per_station = config_json["max_frames_per_station"];
        }
        
        if (config_json.contains("thread_pool_size")) {
            config_.thread_pool_size = config_json["thread_pool_size"];
        }
    } catch (const std::exception& e) {
        log_error("Failed to load config: " + std::string(e.what()));
        save_config();
    }
}

FrameFetcherConfig BackgroundFrameFetcher::get_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

void BackgroundFrameFetcher::set_config(const FrameFetcherConfig& config) {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = config;
    }
    save_config();
}

} // namespace leveliii
