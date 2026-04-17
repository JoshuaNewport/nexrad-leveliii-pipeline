#pragma once

#include <string>
#include <memory>
#include <atomic>

namespace leveliii {

class BackgroundFrameFetcher;
class FrameStorageManager;
class WebServer;
class AdminAPI;

class AdminServer {
public:
    explicit AdminServer(
        std::shared_ptr<BackgroundFrameFetcher> fetcher = nullptr,
        std::shared_ptr<FrameStorageManager> storage = nullptr,
        int port = 13480
    );

    ~AdminServer();

    void start();
    void stop();
    void shutdown_all();
    bool is_running() const { return is_running_; }
    
private:
    std::shared_ptr<BackgroundFrameFetcher> fetcher_;
    std::shared_ptr<FrameStorageManager> storage_;
    int port_;
    std::atomic<bool> is_running_{false};
    std::unique_ptr<WebServer> web_server_;
    std::unique_ptr<AdminAPI> api_;
};

} // namespace leveliii
