#include "leveliii/FrameStorageManager.h"
#include "leveliii/ZlibUtils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <mutex>

namespace leveliii {

namespace {
    bool VERBOSE_LOGGING = false;

    void log_info(const std::string& msg) {
        if (VERBOSE_LOGGING) std::cout << "ℹ️  " << msg << std::endl;
    }

    void set_verbose_logging(bool verbose) {
        VERBOSE_LOGGING = verbose;
    }

    void log_error(const std::string& msg) {
        std::cerr << "❌ " << msg << std::endl;
    }
}

FrameStorageManager::FrameStorageManager(const std::string& base_path)
    : base_path_(base_path) {
    ensure_directory_exists(base_path_);

    size_t usage = 0;
    int count = 0;
    if (fs::exists(base_path_)) {
        for (const auto& entry : fs::recursive_directory_iterator(base_path_)) {
            if (entry.is_regular_file()) {
                usage += entry.file_size();
                if (entry.path().extension() == ".RDA") {
                    count++;
                }
            }
        }
    }
    total_disk_usage_.store(usage);
    total_frame_count_.store(count);

    async_storage_running_.store(true);
    async_storage_stop_.store(false);
    storage_thread_ = std::thread([this]() { this->async_storage_loop(); });
    log_info("FrameStorageManager initialized with async storage thread, base_path=" + base_path_);
}

FrameStorageManager::~FrameStorageManager() {
    shutdown_async_storage();
}

void FrameStorageManager::enqueue_async_write(AsyncWriteTask&& task) {
    log_info("Enqueuing async write: " + task.product_name + " station=" + task.station);
    {
        std::unique_lock<std::mutex> lock(write_queue_mutex_);
        write_queue_full_cv_.wait(lock, [this]() {
            return write_queue_.size() < MAX_WRITE_QUEUE_SIZE || async_storage_stop_.load();
        });

        if (async_storage_stop_.load()) {
            log_error("Storage stopped, cannot enqueue task");
            return;
        }

        write_queue_.push(std::move(task));
        log_info("Task enqueued, queue size now: " + std::to_string(write_queue_.size()));
    }
    write_queue_cv_.notify_one();
}

void FrameStorageManager::shutdown_async_storage() {
    if (!async_storage_running_.load()) return;

    {
        std::lock_guard<std::mutex> lock(write_queue_mutex_);
        async_storage_stop_.store(true);
    }
    write_queue_cv_.notify_all();
    write_queue_full_cv_.notify_all();

    if (storage_thread_.joinable()) {
        storage_thread_.join();
    }

    async_storage_running_.store(false);
}

void FrameStorageManager::async_storage_loop() {
    while (true) {
        AsyncWriteTask task;
        {
            std::unique_lock<std::mutex> lock(write_queue_mutex_);
            write_queue_cv_.wait(lock, [this]() {
                return async_storage_stop_.load() || !write_queue_.empty();
            });

            if (async_storage_stop_.load() && write_queue_.empty()) {
                break;
            }

            if (!write_queue_.empty()) {
                task = std::move(write_queue_.front());
                write_queue_.pop();
                write_queue_full_cv_.notify_one();
            } else {
                continue;
            }
        }

        if (VERBOSE_LOGGING) {
            std::cout << "ℹ️  Processing async write task: product=" << task.product_name
                      << " station=" << task.station << " timestamp=" << task.timestamp
                      << " elevation=" << task.elevation << std::endl;
        }
        process_write_task(task);
    }
}

void FrameStorageManager::process_write_task(const AsyncWriteTask& task) {
    try {
        switch (task.type) {
            case AsyncWriteTask::BITMASK:
                save_frame_bitmask(task.station, task.product_code, task.product_name, task.timestamp, task.elevation,
                                  task.num_rays, task.num_gates, task.gate_spacing, task.first_gate,
                                  task.bitmask, task.values);
                break;
            case AsyncWriteTask::VOLUMETRIC_BITMASK:
                save_volumetric_bitmask(task.station, task.product_code, task.product_name, task.timestamp,
                                       task.elevations, task.num_rays, task.num_gates,
                                       task.gate_spacing, task.first_gate,
                                       task.bitmask, task.values);
                break;
        }
    } catch (const std::exception& e) {
        log_error("Error processing async write task: " + std::string(e.what()));
    }
}

bool FrameStorageManager::ensure_directory_exists(const std::string& path) const {
    try {
        if (!fs::exists(path)) {
            fs::create_directories(path);
        }
        return fs::is_directory(path);
    } catch (const std::exception& e) {
        log_error("Failed to create directory " + path + ": " + e.what());
        return false;
    }
}

std::string FrameStorageManager::get_frame_path(const std::string& station, int16_t product_code, const std::string& product_name, const std::string& timestamp, float elevation) const {
    const char* storage_category = GetProductStorageCategory(product_code);
    std::ostringstream oss;
    oss << base_path_ << "/" << station << "/" << storage_category << "/" << timestamp << "/" << std::fixed << std::setprecision(1) << elevation << ".RDA";
    return oss.str();
}

std::string FrameStorageManager::get_index_path(const std::string& station, const std::string& storage_category) const {
    std::ostringstream oss;
    oss << base_path_ << "/" << station << "/index_" << storage_category << ".json";
    return oss.str();
}

bool FrameStorageManager::save_frame_bitmask(const std::string& station, int16_t product_code, const std::string& product_name, const std::string& timestamp, float elevation, uint16_t num_rays, uint16_t num_gates, float gate_spacing, float first_gate, const std::vector<uint8_t>& bitmask, const std::vector<uint8_t>& values, bool auto_update_index) {
    log_info("save_frame_bitmask: " + station + "/" + product_name + "/" + timestamp + " elev=" + std::to_string(elevation) + " rays=" + std::to_string(num_rays) + " gates=" + std::to_string(num_gates) + " values=" + std::to_string(values.size()));

    const char* storage_category = GetProductStorageCategory(product_code);
    std::string dir = base_path_ + "/" + station + "/" + storage_category + "/" + timestamp;
    if (!ensure_directory_exists(dir)) {
        log_error("Failed to create directory: " + dir);
        return false;
    }

    json metadata = {
        {"s", station}, {"pc", product_code}, {"p", product_name}, {"t", timestamp}, {"e", elevation},
        {"f", "b"}, {"r", num_rays}, {"g", num_gates}, {"gs", gate_spacing},
        {"fg", first_gate}, {"v", values.size()}
    };

    std::string metadata_str = metadata.dump();
    uint32_t metadata_size = (uint32_t)metadata_str.size();

    std::vector<uint8_t> uncompressed;
    uncompressed.reserve(4 + metadata_size + bitmask.size() + values.size());
    uncompressed.insert(uncompressed.end(), reinterpret_cast<uint8_t*>(&metadata_size), reinterpret_cast<uint8_t*>(&metadata_size) + 4);
    uncompressed.insert(uncompressed.end(), metadata_str.begin(), metadata_str.end());
    uncompressed.insert(uncompressed.end(), bitmask.begin(), bitmask.end());
    uncompressed.insert(uncompressed.end(), values.begin(), values.end());

    auto compressed = ZlibUtils::gzip_compress(uncompressed.data(), uncompressed.size());
    if (compressed.empty()) return false;

    std::string file_path = get_frame_path(station, product_code, product_name, timestamp, elevation);
    log_info("Writing to file: " + file_path + " size=" + std::to_string(compressed.size()));

    bool existed = fs::exists(file_path);
    size_t old_size = existed ? fs::file_size(file_path) : 0;

    std::ofstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        log_error("Failed to open file for writing: " + file_path);
        return false;
    }
    file.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    if (!file.good()) {
        log_error("Failed to write to file: " + file_path);
        file.close();
        return false;
    }
    file.close();

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (!existed) {
            total_frame_count_++;
        }
        total_disk_usage_ += (compressed.size() - old_size);
    }

    if (auto_update_index) {
        const char* storage_category = GetProductStorageCategory(product_code);
        update_index(station, storage_category);
    }
    log_info("Successfully saved: " + file_path);
    return true;
}

bool FrameStorageManager::save_volumetric_bitmask(const std::string& station, int16_t product_code, const std::string& product_name, const std::string& timestamp, const std::vector<float>& elevations, uint16_t num_rays, uint16_t num_gates, float gate_spacing, float first_gate, const std::vector<uint8_t>& bitmask, const std::vector<uint8_t>& values, bool auto_update_index) {
    const char* storage_category = GetProductStorageCategory(product_code);
    std::string dir = base_path_ + "/" + station + "/" + storage_category + "/" + timestamp;
    if (!ensure_directory_exists(dir)) return false;

    json metadata = {
        {"s", station}, {"p", product_name}, {"t", timestamp},
        {"f", "b"}, {"tilts", elevations}, {"r", num_rays}, {"g", num_gates},
        {"gs", gate_spacing}, {"fg", first_gate}, {"v", values.size()}
    };

    std::string metadata_str = metadata.dump();
    uint32_t metadata_size = (uint32_t)metadata_str.size();

    std::vector<uint8_t> uncompressed;
    uncompressed.reserve(4 + metadata_size + bitmask.size() + values.size());
    uncompressed.insert(uncompressed.end(), reinterpret_cast<uint8_t*>(&metadata_size), reinterpret_cast<uint8_t*>(&metadata_size) + 4);
    uncompressed.insert(uncompressed.end(), metadata_str.begin(), metadata_str.end());
    uncompressed.insert(uncompressed.end(), bitmask.begin(), bitmask.end());
    uncompressed.insert(uncompressed.end(), values.begin(), values.end());

    auto compressed = ZlibUtils::gzip_compress(uncompressed.data(), uncompressed.size());
    if (compressed.empty()) return false;

    std::string file_path = dir + "/volumetric.RDA";

    bool existed = fs::exists(file_path);
    size_t old_size = existed ? fs::file_size(file_path) : 0;

    std::ofstream file(file_path, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    file.close();

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (!existed) {
            total_frame_count_++;
        }
        total_disk_usage_ += (compressed.size() - old_size);
    }

    if (auto_update_index) {
        const char* storage_category = GetProductStorageCategory(product_code);
        update_index(station, storage_category);
    }
    return true;
}

void FrameStorageManager::update_index(const std::string& station, const std::string& storage_category) {
    try {
        std::unique_lock<std::shared_mutex> lock(index_mutex_);
        auto frames = scan_directory(station, storage_category);

        json index = {
            {"s", station}, {"c", storage_category},
            {"u", std::chrono::system_clock::now().time_since_epoch().count()},
            {"c", frames.size()}, {"f", json::array()}
        };

        for (const auto& frame : frames) {
            index["f"].push_back({{"t", frame.timestamp}, {"e", frame.elevation}});
        }

        std::string json_str = index.dump();
        auto compressed = ZlibUtils::gzip_compress(reinterpret_cast<const uint8_t*>(json_str.c_str()), json_str.size());

        std::string index_path = get_index_path(station, storage_category);
        ensure_directory_exists(fs::path(index_path).parent_path().string());

        std::ofstream file(index_path, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            file.close();
        }

        std::string key = station + "/" + storage_category;
        index_cache_[key] = index;

        if (index_lru_map_.count(key)) {
            index_lru_list_.erase(index_lru_map_[key]);
        }
        index_lru_list_.push_front(key);
        index_lru_map_[key] = index_lru_list_.begin();

        if (index_cache_.size() > MAX_INDEX_CACHE_SIZE) {
            std::string oldest_key = index_lru_list_.back();
            index_cache_.erase(oldest_key);
            index_lru_map_.erase(oldest_key);
            index_lru_list_.pop_back();
        }
    } catch (...) {}
}

json FrameStorageManager::get_index(const std::string& station, const std::string& storage_category) const {
    std::unique_lock<std::shared_mutex> lock(index_mutex_);
    std::string key = station + "/" + storage_category;
    if (index_cache_.count(key)) {
        index_lru_list_.erase(index_lru_map_.at(key));
        index_lru_list_.push_front(key);
        index_lru_map_[key] = index_lru_list_.begin();
        return index_cache_.at(key);
    }

    std::string index_path = get_index_path(station, storage_category);
    if (!fs::exists(index_path)) return json::object();

    std::ifstream file(index_path, std::ios::binary);
    if (!file) return json::object();

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> compressed(size);
    file.read(reinterpret_cast<char*>(compressed.data()), size);
    file.close();

    auto decompressed = ZlibUtils::gzip_decompress(compressed.data(), compressed.size());
    try {
        std::string json_str(decompressed.begin(), decompressed.end());
        json index = json::parse(json_str);

        index_cache_[key] = index;
        index_lru_list_.push_front(key);
        index_lru_map_[key] = index_lru_list_.begin();
        return index;
    } catch (...) {
        return json::object();
    }
}

std::vector<FrameStorageManager::FrameMetadata> FrameStorageManager::scan_directory(const std::string& station, const std::string& storage_category) const {
    std::vector<FrameMetadata> frames;

    std::string station_path = base_path_ + "/" + station + "/" + storage_category;
    if (!fs::exists(station_path)) return frames;

    for (const auto& ts_entry : fs::directory_iterator(station_path)) {
        if (!ts_entry.is_directory()) continue;

        std::string timestamp = ts_entry.path().filename().string();

        for (const auto& f_entry : fs::directory_iterator(ts_entry.path())) {
            if (f_entry.is_regular_file() && f_entry.path().extension() == ".RDA" && f_entry.path().filename() != "volumetric.RDA") {
                std::string filename = f_entry.path().stem().string();

                FrameMetadata meta;
                meta.station = station;
                meta.product_name = storage_category;
                meta.timestamp = timestamp;
                try {
                    meta.elevation = std::stof(filename);
                    meta.file_size = f_entry.file_size();
                    meta.file_path = f_entry.path().string();
                    frames.push_back(meta);
                } catch (...) {}
            }
        }
    }

    std::sort(frames.begin(), frames.end(), [](const FrameMetadata& a, const FrameMetadata& b) {
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
        return a.elevation < b.elevation;
    });

    return frames;
}

size_t FrameStorageManager::get_total_disk_usage() const {
    return total_disk_usage_.load();
}

int FrameStorageManager::get_frame_count() const {
    return total_frame_count_.load();
}

} // namespace leveliii
