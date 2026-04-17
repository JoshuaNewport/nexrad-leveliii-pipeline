#include "leveliii/admin/AdminServer.h"
#include "leveliii/admin/WebServer.h"
#include "leveliii/admin/AdminAPI.h"
#include "leveliii/BackgroundFrameFetcher.h"
#include "leveliii/FrameStorageManager.h"
#include <iostream>

namespace leveliii {

AdminServer::AdminServer(
    std::shared_ptr<BackgroundFrameFetcher> fetcher,
    std::shared_ptr<FrameStorageManager> storage,
    int port
) : fetcher_(fetcher), storage_(storage), port_(port) {}

AdminServer::~AdminServer() {
    stop();
}

void AdminServer::start() {
    if (is_running_) return;

    is_running_ = true;
    web_server_ = std::make_unique<WebServer>("0.0.0.0", port_);
    api_ = std::make_unique<AdminAPI>(fetcher_, storage_);
    
    api_->register_routes(*web_server_);
    
    web_server_->start();
    std::cout << "📊 Admin panel started on http://localhost:" << port_ << std::endl;
}

void AdminServer::stop() {
    if (!is_running_) return;
    
    is_running_ = false;
    if (web_server_) {
        web_server_->stop();
        web_server_.reset();
    }
    api_.reset();
}

void AdminServer::shutdown_all() {
    if (fetcher_) {
        std::cout << "🛑 Shutting down fetcher..." << std::endl;
        fetcher_->stop();
    }
    stop();
    std::cout << "✅ All services stopped" << std::endl;
}

} // namespace leveliii
