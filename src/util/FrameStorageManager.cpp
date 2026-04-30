#include "leveliii/FrameStorageManager.h"
#include "leveliii/ZlibUtils.h"
#include "leveliii/ProductDatabase.h"
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
    {
        std::lock_guard<std::mutex> lock(base_path_mutex_);
        ensure_directory_exists(base_path_);
    }

    // Initialize SQLite database
    std::string db_path = base_path_ + "/index.db";
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        log_error("Failed to open database: " + std::string(sqlite3_errmsg(db_)));
    } else {
        // Enable WAL mode for better concurrency
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        
        // Create frames table
        const char* create_table_sql = 
            "CREATE TABLE IF NOT EXISTS frames ("
            "station TEXT, "
            "product_code INTEGER, "
            "product_name TEXT, "
            "timestamp TEXT, "
            "elevation REAL, "
            "metadata TEXT, "
            "PRIMARY KEY (station, product_name, timestamp, elevation)"
            ");";
        
        char* err_msg = nullptr;
        if (sqlite3_exec(db_, create_table_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            log_error("SQL error: " + std::string(err_msg));
            sqlite3_free(err_msg);
        }
        
        // Create indexes for faster querying
        sqlite3_exec(db_, "CREATE INDEX IF NOT EXISTS idx_station_product ON frames(station, product_name);", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "CREATE INDEX IF NOT EXISTS idx_timestamp ON frames(timestamp);", nullptr, nullptr, nullptr);
    }

    size_t usage = 0;
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(base_path_mutex_);
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
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
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
    {
        std::lock_guard<std::mutex> lock(base_path_mutex_);
        oss << base_path_ << "/" << station << "/" << storage_category << "/" << timestamp << "/" << std::fixed << std::setprecision(1) << elevation << ".RDA";
    }
    return oss.str();
}

bool FrameStorageManager::save_frame_bitmask(const std::string& station, int16_t product_code, const std::string& product_name, const std::string& timestamp, float elevation, uint16_t num_rays, uint16_t num_gates, float gate_spacing, float first_gate, const std::vector<uint8_t>& bitmask, const std::vector<uint8_t>& values, bool auto_update_index) {
    log_info("save_frame_bitmask: " + station + "/" + product_name + "/" + timestamp + " elev=" + std::to_string(elevation) + " rays=" + std::to_string(num_rays) + " gates=" + std::to_string(num_gates) + " values=" + std::to_string(values.size()));

    const char* storage_category = GetProductStorageCategory(product_code);
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(base_path_mutex_);
        dir = base_path_ + "/" + station + "/" + storage_category + "/" + timestamp;
    }
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

    if (db_) {
        const char* sql = "INSERT OR REPLACE INTO frames (station, product_code, product_name, timestamp, elevation, metadata) VALUES (?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, station.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, product_code);
            sqlite3_bind_text(stmt, 3, storage_category, -1, SQLITE_TRANSIENT); // Use storage_category as product_name
            sqlite3_bind_text(stmt, 4, timestamp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(stmt, 5, elevation);
            sqlite3_bind_text(stmt, 6, metadata_str.c_str(), -1, SQLITE_TRANSIENT);

            int rc;
            int retries = 0;
            while ((rc = sqlite3_step(stmt)) == SQLITE_BUSY && retries < 10) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                retries++;
            }

            if (rc != SQLITE_DONE) {
                log_error("Failed to insert frame into database: " + std::string(sqlite3_errmsg(db_)));
            }
            sqlite3_finalize(stmt);
        } else {
            log_error("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
        }
    }

    log_info("Successfully saved: " + file_path);
    return true;
}

bool FrameStorageManager::save_volumetric_bitmask(const std::string& station, int16_t product_code, const std::string& product_name, const std::string& timestamp, const std::vector<float>& elevations, uint16_t num_rays, uint16_t num_gates, float gate_spacing, float first_gate, const std::vector<uint8_t>& bitmask, const std::vector<uint8_t>& values, bool auto_update_index) {
    const char* storage_category = GetProductStorageCategory(product_code);
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(base_path_mutex_);
        dir = base_path_ + "/" + station + "/" + storage_category + "/" + timestamp;
    }
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

    if (db_) {
        const char* sql = "INSERT OR REPLACE INTO frames (station, product_code, product_name, timestamp, elevation, metadata) VALUES (?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, station.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, product_code);
            sqlite3_bind_text(stmt, 3, storage_category, -1, SQLITE_TRANSIENT); // Use storage_category as product_name
            sqlite3_bind_text(stmt, 4, timestamp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(stmt, 5, -1.0); // Use -1.0 for volumetric
            sqlite3_bind_text(stmt, 6, metadata_str.c_str(), -1, SQLITE_TRANSIENT);

            int rc;
            int retries = 0;
            while ((rc = sqlite3_step(stmt)) == SQLITE_BUSY && retries < 10) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                retries++;
            }

            if (rc != SQLITE_DONE) {
                log_error("Failed to insert volumetric frame into database: " + std::string(sqlite3_errmsg(db_)));
            }
            sqlite3_finalize(stmt);
        }
    }

    return true;
}

json FrameStorageManager::get_index(const std::string& station, const std::string& storage_category) const {
    if (!db_) return json::object();

    json index = {
        {"s", station}, {"c", storage_category},
        {"u", std::chrono::system_clock::now().time_since_epoch().count()},
        {"f", json::array()}
    };

    const char* sql = "SELECT timestamp, elevation FROM frames WHERE station = ? AND product_name = ? ORDER BY timestamp ASC, elevation ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, station.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, storage_category.c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string ts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            double elev = sqlite3_column_double(stmt, 1);
            index["f"].push_back({{"t", ts}, {"e", elev}});
        }
        sqlite3_finalize(stmt);
    }
    
    index["count"] = index["f"].size();
    return index;
}

bool FrameStorageManager::has_timestamp_product(const std::string& station, const std::string& product_name, const std::string& timestamp) const {
    if (!db_) return false;

    const char* sql = "SELECT 1 FROM frames WHERE station = ? AND product_name = ? AND timestamp = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    bool exists = false;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, station.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, product_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, timestamp.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            exists = true;
        }
        sqlite3_finalize(stmt);
    }
    return exists;
}

std::vector<FrameStorageManager::FrameMetadata> FrameStorageManager::scan_directory(const std::string& station, const std::string& storage_category) const {
    std::vector<FrameMetadata> frames;

    std::string station_path;
    {
        std::lock_guard<std::mutex> lock(base_path_mutex_);
        station_path = base_path_ + "/" + station + "/" + storage_category;
    }
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

void FrameStorageManager::cleanup_old_frames(int max_frames_per_station) {
    log_info("Running cleanup, max_frames_per_station=" + std::to_string(max_frames_per_station));
    
    {
        std::lock_guard<std::mutex> lock(base_path_mutex_);
        if (!fs::exists(base_path_)) return;
    }

    try {
        std::string current_base_path;
        {
            std::lock_guard<std::mutex> lock(base_path_mutex_);
            current_base_path = base_path_;
        }
        
        for (const auto& station_entry : fs::directory_iterator(current_base_path)) {
            if (!station_entry.is_directory()) continue;
            std::string station = station_entry.path().filename().string();
            
            for (const auto& cat_entry : fs::directory_iterator(station_entry.path())) {
                if (!cat_entry.is_directory()) continue;
                std::string storage_category = cat_entry.path().filename().string();
                
                if (storage_category.find("index_") == 0) continue;

                auto frames = scan_directory(station, storage_category);
                if (frames.empty()) continue;

                std::map<std::string, std::vector<FrameMetadata>> frames_by_timestamp;
                for (const auto& f : frames) {
                    frames_by_timestamp[f.timestamp].push_back(f);
                }
                
                if (frames_by_timestamp.size() <= (size_t)max_frames_per_station) continue;
                
                std::vector<std::string> sorted_timestamps;
                for (auto const& [ts, _] : frames_by_timestamp) {
                    sorted_timestamps.push_back(ts);
                }
                
                size_t num_to_delete = sorted_timestamps.size() - max_frames_per_station;
                log_info("Cleaning up " + std::to_string(num_to_delete) + " timestamps for " + station + "/" + storage_category);

                for (size_t i = 0; i < num_to_delete; ++i) {
                    const std::string& ts_to_delete = sorted_timestamps[i];
                    std::string ts_dir;
                    {
                        std::lock_guard<std::mutex> lock(base_path_mutex_);
                        ts_dir = base_path_ + "/" + station + "/" + storage_category + "/" + ts_to_delete;
                    }
                    
                    size_t deleted_size = 0;
                    int deleted_count = 0;
                    
                    for (const auto& f : frames_by_timestamp[ts_to_delete]) {
                        if (fs::exists(f.file_path)) {
                            deleted_size += fs::file_size(f.file_path);
                            fs::remove(f.file_path);
                            deleted_count++;
                        }
                    }
                    
                    std::string vol_path = ts_dir + "/volumetric.RDA";
                    if (fs::exists(vol_path)) {
                        deleted_size += fs::file_size(vol_path);
                        fs::remove(vol_path);
                        deleted_count++;
                    }
                    
                    if (fs::exists(ts_dir)) {
                        fs::remove_all(ts_dir);
                    }
                    
                    if (db_) {
                        const char* sql = "DELETE FROM frames WHERE station = ? AND product_name = ? AND timestamp = ?;";
                        sqlite3_stmt* stmt;
                        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(stmt, 1, station.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, storage_category.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 3, ts_to_delete.c_str(), -1, SQLITE_TRANSIENT);
                            
                            int rc;
                            int retries = 0;
                            while ((rc = sqlite3_step(stmt)) == SQLITE_BUSY && retries < 10) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                retries++;
                            }
                            sqlite3_finalize(stmt);
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(stats_mutex_);
                        total_disk_usage_ -= deleted_size;
                        total_frame_count_ -= deleted_count;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        log_error("Cleanup failed: " + std::string(e.what()));
    }
}

std::string FrameStorageManager::get_base_path() const {
    std::lock_guard<std::mutex> lock(base_path_mutex_);
    return base_path_;
}

void FrameStorageManager::set_base_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(base_path_mutex_);
    base_path_ = path;
    ensure_directory_exists(base_path_);
}

size_t FrameStorageManager::get_total_disk_usage() const {
    return total_disk_usage_.load();
}

int FrameStorageManager::get_frame_count() const {
    return total_frame_count_.load();
}

} // namespace leveliii
