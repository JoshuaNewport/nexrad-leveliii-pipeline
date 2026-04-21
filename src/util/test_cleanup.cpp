#include "leveliii/FrameStorageManager.h"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace leveliii;

void setup_test_data(FrameStorageManager& manager, const std::string& station, int num_timestamps) {
    std::vector<uint8_t> bitmask(100, 0);
    std::vector<uint8_t> values(100, 1);
    
    for (int i = 1; i <= num_timestamps; ++i) {
        std::string timestamp = "20230101-0000" + (i < 10 ? "0" + std::to_string(i) : std::to_string(i));
        std::cout << "Saving frame for timestamp: " << timestamp << std::endl;
        bool success = manager.save_frame_bitmask(
            station, 
            94, // N0Q
            "REFLECTIVITY", 
            timestamp, 
            0.5f, 
            360, 
            1000, 
            0.25f, 
            2.125f, 
            bitmask, 
            values, 
            true
        );
        assert(success);
    }
}

int main() {
    std::string test_path = "./test_data_cleanup";
    if (fs::exists(test_path)) {
        fs::remove_all(test_path);
    }
    
    {
        FrameStorageManager manager(test_path);
        std::string station = "KDOX";
        int num_timestamps = 10;
        int max_frames = 5;
        
        std::cout << "Setting up " << num_timestamps << " timestamps for station " << station << std::endl;
        setup_test_data(manager, station, num_timestamps);
        
        // Wait a bit for async tasks if any (though save_frame_bitmask is sync in current implementation, 
        // but FrameStorageManager has an async thread)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        std::cout << "Total frames before cleanup: " << manager.get_frame_count() << std::endl;
        assert(manager.get_frame_count() == num_timestamps);
        
        std::cout << "Running cleanup with max_frames=" << max_frames << std::endl;
        manager.cleanup_old_frames(max_frames);
        
        std::cout << "Total frames after cleanup: " << manager.get_frame_count() << std::endl;
        // It should have cleaned up 5 timestamps
        assert(manager.get_frame_count() == max_frames);
        
        // Verify only newest 5 timestamps remain
        // Oldest were 01 to 05, newest 06 to 10
        for (int i = 1; i <= 10; ++i) {
            std::string timestamp = "20230101-0000" + (i < 10 ? "0" + std::to_string(i) : std::to_string(i));
            // Category for 94 is "reflectivity"
            std::string path = test_path + "/" + station + "/reflectivity/" + timestamp;
            if (i <= 5) {
                if (fs::exists(path)) {
                    std::cerr << "Error: Old timestamp " << timestamp << " still exists!" << std::endl;
                    return 1;
                }
            } else {
                if (!fs::exists(path)) {
                    std::cerr << "Error: New timestamp " << timestamp << " is missing!" << std::endl;
                    return 1;
                }
            }
        }
        
        std::cout << "Verifying index update..." << std::endl;
        json index = manager.get_index(station, "reflectivity");
        if (index.is_null() || !index.contains("f")) {
            std::cerr << "Error: Index is null or missing frames array!" << std::endl;
            return 1;
        }
        
        if (index["f"].size() != (size_t)max_frames) {
            std::cerr << "Error: Index has " << index["f"].size() << " frames, expected " << max_frames << "!" << std::endl;
            return 1;
        }
        
        for (const auto& f : index["f"]) {
            std::string ts = f["t"];
            int min_ts = 6;
            int ts_num = std::stoi(ts.substr(ts.size() - 2));
            if (ts_num < min_ts) {
                std::cerr << "Error: Index contains old timestamp: " << ts << std::endl;
                return 1;
            }
        }
        
        std::cout << "Verification successful!" << std::endl;
    }
    
    fs::remove_all(test_path);
    return 0;
}
