#include "LevelIII_Parser.h"
#include "RdaWriter.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_dir_or_file> <output_dir>" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_dir = argv[2];

    if (!fs::exists(output_dir)) {
        if (!fs::create_directories(output_dir)) {
            std::cerr << "Error: Could not create output directory: " << output_dir << std::endl;
            return 1;
        }
    }

    std::vector<fs::path> files;
    if (fs::is_directory(input_path)) {
        for (const auto& entry : fs::directory_iterator(input_path)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path());
            }
        }
    } else {
        files.push_back(input_path);
    }

    std::cout << "Processing " << files.size() << " files..." << std::endl;

    int success_count = 0;
    for (const auto& file : files) {
        leveliii::Parser parser;
        if (parser.LoadFile(file.string())) {
            leveliii::RadarFrame frame = parser.ExtractRadarFrame();
            
            const char* storage_category = leveliii::GetProductStorageCategory(frame.product_code);
            const char* product_name = leveliii::GetProductName(frame.product_code);
            
            fs::path station_dir = fs::path(output_dir) / frame.station_id;
            if (!fs::exists(station_dir)) {
                if (!fs::create_directories(station_dir)) {
                    std::cerr << "Failed to create station directory: " << station_dir << std::endl;
                    continue;
                }
            }
            
            fs::path category_dir = station_dir / storage_category;
            if (!fs::exists(category_dir)) {
                if (!fs::create_directories(category_dir)) {
                    std::cerr << "Failed to create category directory: " << category_dir << std::endl;
                    continue;
                }
            }
            
            fs::path timestamp_dir = category_dir / frame.timestamp;
            if (!fs::exists(timestamp_dir)) {
                if (!fs::create_directories(timestamp_dir)) {
                    std::cerr << "Failed to create timestamp directory: " << timestamp_dir << std::endl;
                    continue;
                }
            }
            
            std::ostringstream elev_str;
            elev_str << std::fixed << std::setprecision(1) << frame.elevation;
            std::string out_name = elev_str.str() + ".RDA";
            fs::path out_path = timestamp_dir / out_name;
            
            if (leveliii::RdaWriter::Write(out_path.string(), frame)) {
                std::cout << "Successfully processed " << file.filename() << " -> " << frame.station_id << "/" << storage_category << "/" << frame.timestamp << "/" << out_name << std::endl;
                success_count++;
            } else {
                std::cerr << "Failed to write " << out_name << std::endl;
            }
        } else {
            std::cerr << "Failed to parse " << file.filename() << std::endl;
        }
    }

    std::cout << "Finished. Successfully processed " << success_count << "/" << files.size() << " files." << std::endl;

    return 0;
}
