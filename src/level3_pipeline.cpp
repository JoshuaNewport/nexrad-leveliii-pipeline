#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <csignal>
#include <unistd.h>
#include <atomic>
#include <thread>

#include "leveliii/BackgroundFrameFetcher.h"
#include "leveliii/FrameStorageManager.h"
#include "leveliii/admin/AdminServer.h"
#include "leveliii/TerminalUI.h"
#include "leveliii/AWSInitializer.h"

namespace leveliii {

static std::atomic<bool> shutdown_requested(false);

void signal_handler(int signum) {
    std::cout << "\n🛑 Received signal " << signum << ", shutting down..." << std::endl;
    shutdown_requested = true;
}

std::string get_executable_directory() {
    char result[256];
    ssize_t count = readlink("/proc/self/exe", result, 255);
    if (count != -1) {
        result[count] = '\0';
        std::string exe_path(result);
        size_t pos = exe_path.find_last_of("/");
        return exe_path.substr(0, pos);
    }
    return ".";
}

} // namespace leveliii

int main(int argc, char* argv[]) {
    using namespace leveliii;

    bool use_http = true;
    bool use_quiet = true;
    int cmd_threads = -1;
    std::string cmd_data_dir;
    int port = 13480;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-http") {
            use_http = false;
        } else if (arg == "--verbose") {
            use_quiet = false;
        } else if (arg == "--threads" && i + 1 < argc) {
            cmd_threads = std::stoi(argv[++i]);
        } else if (arg == "--data-dir" && i + 1 < argc) {
            cmd_data_dir = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --no-http              Disable HTTP admin server\n"
                      << "  --port N               Port for HTTP admin server (default: 13480)\n"
                      << "  --threads N            Number of worker threads\n"
                      << "  --data-dir PATH        Directory where Level III data will be stored\n"
                      << "  --verbose              Enable detailed logging output\n"
                      << "  --help                 Show this help message\n";
            return 0;
        }
    }
    
    std::cout << "🚀 Level III Data Pipeline Service Starting" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Initialize AWS SDK
        AWSInitializer aws_init;

        std::string base_dir = get_executable_directory();
        std::string level3_data_path;

        if (!cmd_data_dir.empty()) {
            level3_data_path = cmd_data_dir;
        } else {
            level3_data_path = base_dir + "/data/leveliii";
        }

        std::cout << "📁 Data directory: " << level3_data_path << std::endl;
        std::cout.flush();

        auto storage_manager = std::make_shared<FrameStorageManager>(level3_data_path);
        std::cout << "✅ Storage manager created successfully" << std::endl;
        std::cout.flush();
        
        FrameFetcherConfig fetcher_config;
        if (cmd_threads > 0) fetcher_config.thread_pool_size = cmd_threads;
        
        const char* env_stations = std::getenv("NEXRAD_MONITORED_STATIONS");
        if (env_stations) {
            std::string stations_str(env_stations);
            fetcher_config.monitored_stations.clear();
            size_t start = 0;
            size_t end = stations_str.find(',');
            while (end != std::string::npos) {
                fetcher_config.monitored_stations.insert(stations_str.substr(start, end - start));
                start = end + 1;
                end = stations_str.find(',', start);
            }
            fetcher_config.monitored_stations.insert(stations_str.substr(start));
        }

        std::cout << "📊 Creating BackgroundFrameFetcher (initializing S3 client)..." << std::endl;
        std::cout.flush();
        auto frame_fetcher = std::make_shared<BackgroundFrameFetcher>(storage_manager, fetcher_config);
        std::cout << "✅ BackgroundFrameFetcher created with S3 client ready" << std::endl;
        std::cout.flush();
        
        std::shared_ptr<AdminServer> admin_server;
        if (use_http) {
            std::cout << "🌐 Starting Admin Server on port " << port << "..." << std::endl;
            std::cout.flush();
            admin_server = std::make_shared<AdminServer>(frame_fetcher, storage_manager, port);
            std::cout << "✅ AdminServer instance created" << std::endl;
            std::cout.flush();
            admin_server->start();
            std::cout << "✅ AdminServer started" << std::endl;
            std::cout.flush();
        }

        std::cout << "🔄 Starting data fetcher..." << std::endl;
        std::cout.flush();
        frame_fetcher->start();
        std::cout << "\n✅ Level III data pipeline running. Press Ctrl+C to stop." << std::endl;
        std::cout << std::string(70, '=') << std::endl;
        std::cout.flush();

        auto terminal_ui = std::make_shared<TerminalUI>(frame_fetcher);

        while (!shutdown_requested) {
            terminal_ui->render();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        std::cout << "\n🛑 Shutting down Level III pipeline..." << std::endl;
        
        if (admin_server) {
            admin_server->shutdown_all();
        }
        
        std::cout << "✅ Level III Data Pipeline Service stopped cleanly" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
