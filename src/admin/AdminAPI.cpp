#include "leveliii/admin/AdminAPI.h"
#include "leveliii/admin/WebServer.h"
#include "leveliii/BackgroundFrameFetcher.h"
#include "leveliii/FrameStorageManager.h"
#include "leveliii/Version.h"
#include <chrono>
#include <ctime>

namespace leveliii {

static auto g_start_time = std::chrono::system_clock::now();

AdminAPI::AdminAPI(
    std::shared_ptr<BackgroundFrameFetcher> fetcher,
    std::shared_ptr<FrameStorageManager> storage
) : fetcher_(fetcher), storage_(storage) {}

void AdminAPI::register_routes(WebServer& server) {
    server.add_route("GET", "/api/stations", [this](const std::string&, const std::string&) {
        return handle_get_stations().dump();
    });

    server.add_route("POST", "/api/stations", [this](const std::string& body, const std::string&) {
        return handle_post_stations(body).dump();
    });

    server.add_route("DELETE", "/api/stations/:name", [this](const std::string&, const std::string& param) {
        return handle_delete_station(param).dump();
    });

    server.add_route("GET", "/api/metrics", [this](const std::string&, const std::string&) {
        return handle_get_metrics().dump();
    });

    server.add_route("GET", "/api/status", [this](const std::string&, const std::string&) {
        return handle_get_status().dump();
    });

    server.add_route("GET", "/api/config", [this](const std::string&, const std::string&) {
        return handle_get_config().dump();
    });

    server.add_route("POST", "/api/config", [this](const std::string& body, const std::string&) {
        return handle_post_config(body).dump();
    });

    server.add_route("POST", "/api/storage/path", [this](const std::string& body, const std::string&) {
        return handle_post_storage_path(body).dump();
    });

    server.add_route("POST", "/api/pause", [this](const std::string&, const std::string&) {
        return handle_post_pause().dump();
    });

    server.add_route("POST", "/api/resume", [this](const std::string&, const std::string&) {
        return handle_post_resume().dump();
    });
}

json AdminAPI::handle_get_stations() {
    if (!fetcher_) {
        return json::array();
    }
    auto stations = fetcher_->get_monitored_stations();
    json response = json::array();
    for (const auto& station : stations) {
        response.push_back({
            {"name", station},
            {"status", "active"}
        });
    }
    return response;
}

json AdminAPI::handle_post_stations(const std::string& body) {
    if (!fetcher_) {
        return json{{"error", "Fetcher not initialized"}};
    }
    try {
        auto data = json::parse(body);
        std::string station_name = data.value("name", "");
        
        if (station_name.empty()) {
            return json{{"error", "Station name required"}};
        }
        
        auto stations = fetcher_->get_monitored_stations();
        stations.insert(station_name);
        fetcher_->set_monitored_stations(stations);
        return json{
            {"success", true},
            {"station", station_name}
        };
    } catch (const std::exception& e) {
        return json{{"error", e.what()}};
    }
}

json AdminAPI::handle_delete_station(const std::string& name) {
    if (!fetcher_) {
        return json{{"error", "Fetcher not initialized"}};
    }
    if (name.empty()) {
        return json{{"error", "Station name required"}};
    }
    
    auto stations = fetcher_->get_monitored_stations();
    stations.erase(name);
    fetcher_->set_monitored_stations(stations);
    return json{
        {"success", true},
        {"station", name}
    };
}

json AdminAPI::handle_get_metrics() {
    auto now = std::chrono::system_clock::now();
    auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - g_start_time
    ).count();
    
    json metrics;
    metrics["version"] = LEVEL3_PIPELINE_VERSION;
    
    if (fetcher_) {
        metrics.merge_patch(fetcher_->get_statistics());
        metrics["version"] = LEVEL3_PIPELINE_VERSION;
        
        uint64_t fetched = metrics.value("frames_fetched", 0);
        uint64_t failed = metrics.value("frames_failed", 0);
        
        if (uptime_seconds > 0) {
            metrics["avg_frames_per_minute"] = (double(fetched) / uptime_seconds) * 60.0;
        } else {
            metrics["avg_frames_per_minute"] = 0.0;
        }
        
        if ((fetched + failed) > 0) {
            metrics["success_rate"] = (double(fetched) / (fetched + failed)) * 100.0;
        } else {
            metrics["success_rate"] = 0.0;
        }
    } else {
        metrics = {
            {"version", LEVEL3_PIPELINE_VERSION},
            {"frames_fetched", 0},
            {"frames_failed", 0},
            {"success_rate", 0.0},
            {"avg_frames_per_minute", 0.0},
            {"last_fetch_timestamp", 0}
        };
    }
    
    metrics["uptime_seconds"] = uptime_seconds;
    
    if (storage_) {
        auto disk_usage = storage_->get_total_disk_usage();
        metrics["disk_usage_mb"] = disk_usage / (1024 * 1024);
        metrics["disk_usage_gb"] = (double)disk_usage / (1024 * 1024 * 1024);
        metrics["frame_count"] = storage_->get_frame_count();
        metrics["storage_pending_tasks"] = storage_->num_pending_tasks();
    } else {
        metrics["disk_usage_mb"] = 0;
        metrics["disk_usage_gb"] = 0.0;
        metrics["frame_count"] = 0;
    }
    
    return metrics;
}

json AdminAPI::handle_get_status() {
    bool running = fetcher_ ? fetcher_->is_running() : false;
    return json{
        {"status", "operational"},
        {"fetcher_running", running},
        {"version", LEVEL3_PIPELINE_VERSION},
        {"timestamp", std::time(nullptr)}
    };
}

json AdminAPI::handle_get_config() {
    if (!fetcher_) return json{{"error", "Fetcher not initialized"}};
    
    auto config = fetcher_->get_config();
    return json{
        {"version", LEVEL3_PIPELINE_VERSION},
        {"scan_interval_seconds", config.scan_interval_seconds},
        {"max_frames_per_station", config.max_frames_per_station},
        {"thread_pool_size", config.thread_pool_size},
        {"max_concurrent_discoveries", config.max_concurrent_discoveries},
        {"discovery_catchup_limit", config.discovery_catchup_limit},
        {"discovery_queue_threshold", config.discovery_queue_threshold},
        {"data_lookback_hours", config.data_lookback_hours},
        {"catchup", config.catchup}
    };
}

json AdminAPI::handle_post_config(const std::string& body) {
    if (!fetcher_) return json{{"error", "Fetcher not initialized"}};
    
    try {
        auto data = json::parse(body);
        auto config = fetcher_->get_config();
        
        if (data.contains("scan_interval_seconds")) config.scan_interval_seconds = data["scan_interval_seconds"];
        if (data.contains("max_frames_per_station")) config.max_frames_per_station = data["max_frames_per_station"];
        if (data.contains("thread_pool_size")) config.thread_pool_size = data["thread_pool_size"];
        if (data.contains("max_concurrent_discoveries")) config.max_concurrent_discoveries = data["max_concurrent_discoveries"];
        if (data.contains("discovery_catchup_limit")) config.discovery_catchup_limit = data["discovery_catchup_limit"];
        if (data.contains("discovery_queue_threshold")) config.discovery_queue_threshold = data["discovery_queue_threshold"];
        if (data.contains("data_lookback_hours")) config.data_lookback_hours = data["data_lookback_hours"];
        if (data.contains("catchup")) config.catchup = data["catchup"];
        
        fetcher_->set_config(config);
        return json{{"success", true}, {"config", handle_get_config()}};
    } catch (const std::exception& e) {
        return json{{"error", e.what()}};
    }
}

json AdminAPI::handle_post_pause() {
    if (!fetcher_) {
        return json{{"error", "Fetcher not initialized"}};
    }
    if (!fetcher_->is_running()) {
        return json{{"success", true}, {"status", "already paused"}};
    }
    fetcher_->stop();
    return json{{"success", true}, {"status", "paused"}, {"message", "All threads stopped successfully"}};
}

json AdminAPI::handle_post_resume() {
    if (!fetcher_) {
        return json{{"error", "Fetcher not initialized"}};
    }
    if (fetcher_->is_running()) {
        return json{{"error", "Fetcher already running"}};
    }
    fetcher_->start();
    return json{{"success", true}, {"status", "resumed"}};
}

json AdminAPI::handle_post_storage_path(const std::string& body) {
    if (!storage_) return json{{"error", "Storage manager not initialized"}};
    
    try {
        auto data = json::parse(body);
        std::string new_path = data.value("path", "");
        
        if (new_path.empty()) {
            return json{{"error", "Path is required"}};
        }
        
        storage_->set_base_path(new_path);
        return json{
            {"success", true},
            {"path", storage_->get_base_path()}
        };
    } catch (const std::exception& e) {
        return json{{"error", e.what()}};
    }
}

} // namespace leveliii
