#include "RdaWriter.h"
#include "leveliii/ProductDatabase.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <zlib.h>

namespace leveliii {

struct QuantRange {
    float min;
    float max;
};

static QuantRange GetQuantRange(int16_t product_code) {
    const char* category = GetProductCategory(product_code);
    
    if (strcmp(category, "velocity") == 0 || strcmp(category, "super_res_velocity") == 0) {
        return {-100.0f, 100.0f};
    } else if (strcmp(category, "spectrum_width") == 0) {
        return {0.0f, 64.0f};
    } else if (strcmp(category, "differential_reflectivity") == 0) {
        return {-8.0f, 8.0f};
    } else if (strcmp(category, "specific_differential_phase") == 0) {
        return {0.0f, 360.0f};
    } else if (strcmp(category, "correlation_coefficient") == 0) {
        return {0.0f, 1.1f};
    } else if (strcmp(category, "echo_tops") == 0) {
        return {0.0f, 230.0f};
    } else if (strcmp(category, "vil") == 0) {
        return {0.0f, 75.0f};
    } else if (strcmp(category, "precipitation") == 0) {
        return {0.0f, 24.0f};
    }
    // Default: reflectivity
    return {-32.0f, 94.5f};
}

bool RdaWriter::Write(const std::string& filename, const RadarFrame& frame) {
    if (frame.ray_count == 0 || frame.gate_count == 0 || frame.data.empty()) {
        return false;
    }
    
    uint32_t total_bins = frame.ray_count * frame.gate_count;
    if (frame.data.size() < total_bins) {
        return false;
    }
    
    // 1. Calculate bitmask and valid points
    uint32_t bitmask_size = (total_bins + 7) / 8;
    std::vector<uint8_t> bitmask(bitmask_size, 0);
    std::vector<uint8_t> valid_values;
    valid_values.reserve(total_bins / 2); // Heuristic

    for (uint32_t i = 0; i < total_bins; ++i) {
        if (frame.data[i] > 0) {
            uint32_t byte_idx = i / 8;
            uint32_t bit_pos = 7 - (i % 8);
            bitmask[byte_idx] |= (1 << bit_pos);
            valid_values.push_back(frame.data[i]);
        }
    }

    // 2. Create Metadata JSON
    std::string metadata = CreateMetadataJson(frame, static_cast<uint32_t>(valid_values.size()));
    uint32_t metadata_size = static_cast<uint32_t>(metadata.size());

    // 3. Prepare full buffer for compression
    std::vector<uint8_t> full_buffer;
    full_buffer.resize(4 + metadata_size + bitmask_size + valid_values.size());

    std::memcpy(full_buffer.data(), &metadata_size, 4);
    std::memcpy(full_buffer.data() + 4, metadata.data(), metadata_size);
    std::memcpy(full_buffer.data() + 4 + metadata_size, bitmask.data(), bitmask_size);
    std::memcpy(full_buffer.data() + 4 + metadata_size + bitmask_size, valid_values.data(), valid_values.size());

    // 4. Gzip Compress
    gzFile file = gzopen(filename.c_str(), "wb");
    if (!file) {
        return false;
    }

    int written = gzwrite(file, full_buffer.data(), static_cast<unsigned int>(full_buffer.size()));
    gzclose(file);

    return written == static_cast<int>(full_buffer.size());
}

std::string RdaWriter::CreateMetadataJson(const RadarFrame& frame, uint32_t valid_count) {
    QuantRange qr = GetQuantRange(frame.product_code);
    
    std::stringstream ss;
    ss << "{";
    ss << "\"s\":\"" << frame.station_id << "\",";
    ss << "\"p\":\"" << GetProductCategory(frame.product_code) << "\",";
    ss << "\"t\":\"" << frame.timestamp << "\",";
    ss << "\"e\":" << std::fixed << std::setprecision(2) << frame.elevation << ",";
    ss << "\"f\":\"b\",";
    ss << "\"r\":" << frame.ray_count << ",";
    ss << "\"g\":" << frame.gate_count << ",";
    ss << "\"gs\":" << frame.gate_spacing << ",";
    ss << "\"fg\":" << frame.first_gate_dist << ",";
    ss << "\"v\":" << valid_count << ",";
    ss << "\"qmin\":" << qr.min << ",";
    ss << "\"qmax\":" << qr.max;
    ss << "}";
    return ss.str();
}

} // namespace leveliii
