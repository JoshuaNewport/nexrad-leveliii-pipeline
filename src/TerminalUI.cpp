#include "leveliii/TerminalUI.h"
#include "leveliii/BackgroundFrameFetcher.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <sys/ioctl.h>
#include <unistd.h>

namespace leveliii {

TerminalUI::TerminalUI(std::shared_ptr<BackgroundFrameFetcher> fetcher)
    : fetcher_(fetcher) {}

void TerminalUI::clear_screen() {
    std::cout << "\033[2J\033[H" << std::flush;
}

void TerminalUI::render() {
    if (!fetcher_) return;

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_height = (w.ws_row > 0) ? w.ws_row : 24;
    int term_width = (w.ws_col > 0) ? w.ws_col : 80;

    auto stats = fetcher_->get_statistics();
    
    std::stringstream ui_buffer;
    ui_buffer << "\033[H";

    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
    gmtime_r(&now_time_t, &now_tm);
    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%H:%M:%S UTC", &now_tm);

    std::string line_sep = std::string(std::min(term_width, 80), '=');
    std::string dash_sep = std::string(std::min(term_width, 80), '-');

    ui_buffer << "\033[1;36m" << line_sep << "\033[K\033[0m" << std::endl;
    ui_buffer << "\033[1;37m   NEXRAD Level III Processing Pipeline                " << time_str << "   \033[K\033[0m" << std::endl;
    ui_buffer << "\033[1;36m" << line_sep << "\033[K\033[0m" << std::endl;

    bool is_running = stats.value("is_running", false);
    ui_buffer << " Status: " << (is_running ? "\033[1;32mRUNNING         \033[0m" : "\033[1;31mSTOPPED\033[0m");
    ui_buffer << "    Scan Interval: " << stats.value("scan_interval", 0) << "s";
    
    uint64_t last_fetch_ns = stats.value("last_fetch_timestamp", 0UL);
    ui_buffer << "        Last Activity: " << format_time_short(last_fetch_ns) << "\033[K" << std::endl;
    ui_buffer << dash_sep << "\033[K" << std::endl;

    ui_buffer << "\033[1;33m[ SYSTEM STATISTICS ]\033[0m\033[K" << std::endl;
    ui_buffer << " Total Frames Fetched: " << std::setw(10) << stats.value("frames_fetched", 0UL);
    ui_buffer << "    Failed: " << std::setw(10) << stats.value("frames_failed", 0UL) << "\033[K" << std::endl;
    
    if (stats.contains("total_disk_usage_bytes")) {
        ui_buffer << " Disk Usage: " << std::setw(18) << format_size(stats.value("total_disk_usage_bytes", 0UL));
        ui_buffer << "    Files:  " << std::setw(10) << stats.value("frame_count", 0UL) << "\033[K" << std::endl;
    }
    ui_buffer << dash_sep << "\033[K" << std::endl;

    ui_buffer << "\033[1;33m[ RESOURCE UTILIZATION ]\033[0m\033[K" << std::endl;
    if (stats.contains("thread_pool")) {
        auto tp = stats["thread_pool"];
        int total = tp.value("worker_count", 0);
        int active = tp.value("active_threads", 0);
        ui_buffer << " Fetch Workers: " << std::setw(2) << active << "/" << std::setw(2) << total << " active ";
        ui_buffer << " [";
        for (int i = 0; i < 15; ++i) {
            if (i < (active * 15 / (total > 0 ? total : 1))) ui_buffer << "\033[1;32m#\033[0m";
            else ui_buffer << " ";
        }
        ui_buffer << "]  Tasks: " << tp.value("pending_tasks", 0) << "\033[K" << std::endl;
    }
    if (stats.contains("discovery_pool")) {
        auto dp = stats["discovery_pool"];
        int total = dp.value("worker_count", 0);
        int active = dp.value("active_threads", 0);
        ui_buffer << " Disc. Workers:  " << std::setw(2) << active << "/" << std::setw(2) << total << " active ";
        ui_buffer << " [";
        for (int i = 0; i < 15; ++i) {
            if (i < (active * 15 / (total > 0 ? total : 1))) ui_buffer << "\033[1;34m#\033[0m";
            else ui_buffer << " ";
        }
        ui_buffer << "]  Tasks: " << dp.value("pending_tasks", 0) << "\033[K" << std::endl;
    }
    ui_buffer << dash_sep << "\033[K" << std::endl;

    ui_buffer << "\033[1;33m[ STATION STATUS ]\033[0m\033[K" << std::endl;
    ui_buffer << std::left << std::setw(9) << " Station" 
              << std::right << std::setw(8) << "Fetched" 
              << "   " << std::left << std::setw(18) << "Last Frame" 
              << std::left << std::setw(18) << "Last Fetch"
              << "Last Scan" << "\033[K" << std::endl;
    
    if (stats.contains("station_stats")) {
        auto s_stats = stats["station_stats"];
        std::vector<std::string> stations;
        for (auto it = s_stats.begin(); it != s_stats.end(); ++it) stations.push_back(it.key());
        std::sort(stations.begin(), stations.end());

        int used_lines = 19; 
        int max_display = term_height - used_lines;
        if (max_display < 1) max_display = 1;

        size_t display_count = std::min(stations.size(), static_cast<size_t>(max_display));
        for (size_t i = 0; i < display_count; ++i) {
            const auto& station = stations[i];
            auto s = s_stats[station];
            ui_buffer << " " << std::left << std::setw(8) << station
                      << std::right << std::setw(8) << s.value("frames_fetched", 0UL)
                      << "   " << std::left << std::setw(18) << s.value("last_frame_timestamp", "N/A")
                      << std::left << std::setw(18) << format_time_short(s.value("last_fetch_timestamp", 0UL))
                      << format_time_short(s.value("last_scan_timestamp", 0UL)) << "\033[K" << std::endl;
        }
        if (stations.size() > display_count) {
            ui_buffer << " ... and " << (stations.size() - display_count) << " more stations." << "\033[K" << std::endl;
        }
    } else {
        ui_buffer << " No station data available yet. Scanning S3..." << "\033[K" << std::endl;
    }

    ui_buffer << "\033[1;36m" << line_sep << "\033[K\033[0m" << std::endl;
    ui_buffer << " Press Ctrl+C to shutdown service.\033[K"; 

    ui_buffer << "\033[J";

    std::cout << ui_buffer.str() << std::flush;
}

std::string TerminalUI::format_size(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024 && unit < 4) { size /= 1024; unit++; }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return ss.str();
}

std::string TerminalUI::format_time_short(uint64_t timestamp_ns) {
    if (timestamp_ns == 0) return "N/A";
    std::time_t t = static_cast<std::time_t>(timestamp_ns / 1000000000);
    std::tm tm_res;
    if (!gmtime_r(&t, &tm_res)) return "Err";
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S UTC", &tm_res);
    return std::string(buf);
}

} // namespace leveliii
