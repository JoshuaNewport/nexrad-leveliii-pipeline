#pragma once

#include <memory>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace leveliii {

class WebServer;
class BackgroundFrameFetcher;
class FrameStorageManager;

class AdminAPI {
public:
    explicit AdminAPI(
        std::shared_ptr<BackgroundFrameFetcher> fetcher = nullptr,
        std::shared_ptr<FrameStorageManager> storage = nullptr
    );

    void register_routes(WebServer& server);

private:
    std::shared_ptr<BackgroundFrameFetcher> fetcher_;
    std::shared_ptr<FrameStorageManager> storage_;

    json handle_get_stations();
    json handle_post_stations(const std::string& body);
    json handle_delete_station(const std::string& name);
    json handle_get_metrics();
    json handle_get_status();
    json handle_get_config();
    json handle_post_config(const std::string& body);
    json handle_post_pause();
    json handle_post_resume();
};

} // namespace leveliii
