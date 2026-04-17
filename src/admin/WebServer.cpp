#include "leveliii/admin/WebServer.h"
#include "httplib.h"
#include <thread>
#include <atomic>
#include <iostream>

namespace leveliii {

class WebServer::Impl {
public:
    httplib::Server server;
    std::unique_ptr<std::thread> thread;
    std::atomic<bool> running{false};
};

WebServer::WebServer(const std::string& host, int port)
    : pimpl_(std::make_unique<Impl>()), host_(host), port_(port) {
    pimpl_->server.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });
    
    pimpl_->server.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });
}

WebServer::~WebServer() {
    stop();
}

void WebServer::add_route(const std::string& method, const std::string& path,
                         RequestHandler handler) {
    if (method == "GET") {
        pimpl_->server.Get(path, [handler](const httplib::Request& req, httplib::Response& res) {
            std::string param = "";
            // Simplified param extraction for Level III
            size_t last_slash = req.path.find_last_of('/');
            if (last_slash != std::string::npos && last_slash < req.path.size() - 1) {
                param = req.path.substr(last_slash + 1);
            }
            std::string response = handler("", param);
            res.set_content(response, "application/json");
        });
    } else if (method == "POST") {
        pimpl_->server.Post(path, [handler](const httplib::Request& req, httplib::Response& res) {
            std::string param = "";
            size_t last_slash = req.path.find_last_of('/');
            if (last_slash != std::string::npos && last_slash < req.path.size() - 1) {
                param = req.path.substr(last_slash + 1);
            }
            std::string response = handler(req.body, param);
            res.set_content(response, "application/json");
        });
    } else if (method == "DELETE") {
        pimpl_->server.Delete(path, [handler](const httplib::Request& req, httplib::Response& res) {
            std::string param = "";
            size_t last_slash = req.path.find_last_of('/');
            if (last_slash != std::string::npos && last_slash < req.path.size() - 1) {
                param = req.path.substr(last_slash + 1);
            }
            std::string response = handler(req.body, param);
            res.set_content(response, "application/json");
        });
    }
}

void WebServer::start() {
    if (pimpl_->running) return;
    
    pimpl_->running = true;
    pimpl_->thread = std::make_unique<std::thread>([this]() {
        pimpl_->server.listen(host_.c_str(), port_);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void WebServer::stop() {
    if (!pimpl_->running) return;
    
    pimpl_->running = false;
    pimpl_->server.stop();
    if (pimpl_->thread && pimpl_->thread->joinable()) {
        pimpl_->thread->join();
    }
}

bool WebServer::is_running() const {
    return pimpl_->running;
}

} // namespace leveliii
