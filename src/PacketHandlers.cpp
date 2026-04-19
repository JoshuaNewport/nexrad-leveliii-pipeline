#include "leveliii/PacketHandlers.h"
#include <netinet/in.h>
#include <algorithm>
#include <cstring>

namespace leveliii {

bool RadialPacketHandler::CanHandle(uint16_t packet_code) const {
    return packet_code == 16;
}

bool RadialPacketHandler::Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) {
    uint16_t first_bin, num_bins, i_center, j_center, range_scale, num_radials;
    is.read(reinterpret_cast<char*>(&first_bin), 2);
    is.read(reinterpret_cast<char*>(&num_bins), 2);
    is.read(reinterpret_cast<char*>(&i_center), 2);
    is.read(reinterpret_cast<char*>(&j_center), 2);
    is.read(reinterpret_cast<char*>(&range_scale), 2);
    is.read(reinterpret_cast<char*>(&num_radials), 2);

    frame.ray_count = ntohs(num_radials);
    frame.gate_count = ntohs(num_bins);

    uint16_t actual_range_scale = ntohs(range_scale);
    float multiplier = 0.001f;

    switch (ctx.product_code) {
        case 94:  // DR/N0Q
        case 99:  // DV/N0U
        case 153: // SDR
        case 154: // SDV
        case 155: // SDW
        case 159: // DZD
        case 161: // DCC
        case 163: // DKD
        case 165: // DHC
        case 176: // IPR
        case 177: // HHC
            multiplier = 0.00025f;
            break;
        case 180: // TDR
        case 182: // TDV
            multiplier = 0.00015f;
            break;
        case 186: // TZL
            multiplier = 0.0003f;
            break;
        case 20:  // N0R (some versions)
        case 25:  // N0Z
        case 28:  // N0S
            multiplier = 0.00025f;
            break;
        case 134: // DVL
        case 135: // EET
            multiplier = 1.0f;
            break;
        default:
            multiplier = 0.001f;
            break;
    }

    // If range_scale is 1000 and multiplier is 0.00025, result is 0.25km
    frame.gate_spacing = actual_range_scale * multiplier;
    frame.first_gate_dist = ntohs(first_bin) * 0.001f;

    bool has_data = false;
    if (frame.ray_count > 0 && frame.gate_count > 0) {
        frame.data.assign(frame.ray_count * frame.gate_count, 0);
        has_data = true;
    }

    struct RawRadial {
        uint16_t bytes_or_rle;
        uint16_t start_angle;
        uint16_t delta_angle;
        std::vector<uint8_t> data;
    };
    std::vector<RawRadial> raw_radials(frame.ray_count);

    for (int r = 0; r < frame.ray_count; ++r) {
        is.read(reinterpret_cast<char*>(&raw_radials[r].bytes_or_rle), 2);
        is.read(reinterpret_cast<char*>(&raw_radials[r].start_angle), 2);
        is.read(reinterpret_cast<char*>(&raw_radials[r].delta_angle), 2);
        raw_radials[r].bytes_or_rle = ntohs(raw_radials[r].bytes_or_rle);

        if (has_data) {
            size_t to_read = std::min((size_t)frame.gate_count, (size_t)raw_radials[r].bytes_or_rle);
            raw_radials[r].data.resize(to_read);
            is.read(reinterpret_cast<char*>(raw_radials[r].data.data()), to_read);

            if (raw_radials[r].bytes_or_rle > to_read) {
                is.seekg(raw_radials[r].bytes_or_rle - to_read, std::ios::cur);
            }
        } else {
            is.seekg(raw_radials[r].bytes_or_rle, std::ios::cur);
        }
    }

    if (has_data) {
        std::vector<size_t> indices(frame.ray_count);
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
        std::sort(indices.begin(), indices.end(), [&raw_radials](size_t a, size_t b) {
            return ntohs(raw_radials[a].start_angle) < ntohs(raw_radials[b].start_angle);
        });

        for (size_t i = 0; i < indices.size(); ++i) {
            const auto& src = raw_radials[indices[i]];
            std::copy(src.data.begin(), src.data.end(), frame.data.begin() + i * frame.gate_count);

            if (ctx.product_code == 135) {
                uint8_t mask = static_cast<uint8_t>(ctx.pdb.data_level_thresholds[0]);
                if (mask == 0) mask = 0x7F;
                for (size_t j = 0; j < src.data.size(); ++j) {
                    frame.data[i * frame.gate_count + j] &= mask;
                }
            }
        }
    }

    return has_data;
}

bool RleRadialPacketHandler::CanHandle(uint16_t packet_code) const {
    return packet_code == 0xAF1F;
}

bool RleRadialPacketHandler::Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) {
    uint16_t first_bin, num_bins, i_center, j_center, range_scale, num_radials;
    is.read(reinterpret_cast<char*>(&first_bin), 2);
    is.read(reinterpret_cast<char*>(&num_bins), 2);
    is.read(reinterpret_cast<char*>(&i_center), 2);
    is.read(reinterpret_cast<char*>(&j_center), 2);
    is.read(reinterpret_cast<char*>(&range_scale), 2);
    is.read(reinterpret_cast<char*>(&num_radials), 2);

    frame.ray_count = ntohs(num_radials);
    frame.gate_count = ntohs(num_bins);

    uint16_t actual_range_scale = ntohs(range_scale);
    float multiplier = 0.001f;

    switch (ctx.product_code) {
        case 94:  // DR/N0Q
        case 99:  // DV/N0U
        case 153: // SDR
        case 154: // SDV
        case 155: // SDW
        case 159: // DZD
        case 161: // DCC
        case 163: // DKD
        case 165: // DHC
        case 176: // IPR
        case 177: // HHC
            multiplier = 0.00025f;
            break;
        case 180: // TDR
        case 182: // TDV
            multiplier = 0.00015f;
            break;
        case 186: // TZL
            multiplier = 0.0003f;
            break;
        case 20:  // N0R (some versions)
        case 25:  // N0Z
        case 28:  // N0S
            multiplier = 0.00025f;
            break;
        case 134: // DVL
        case 135: // EET
            multiplier = 1.0f;
            break;
        default:
            multiplier = 0.001f;
            break;
    }

    // If range_scale is 1000 and multiplier is 0.00025, result is 0.25km
    frame.gate_spacing = actual_range_scale * multiplier;
    frame.first_gate_dist = ntohs(first_bin) * 0.001f;

    bool has_data = false;
    if (frame.ray_count > 0 && frame.gate_count > 0) {
        frame.data.assign(frame.ray_count * frame.gate_count, 0);
        has_data = true;
    }

    for (int r = 0; r < frame.ray_count; ++r) {
        uint16_t bytes_or_rle, start_angle, delta_angle;
        is.read(reinterpret_cast<char*>(&bytes_or_rle), 2);
        is.read(reinterpret_cast<char*>(&start_angle), 2);
        is.read(reinterpret_cast<char*>(&delta_angle), 2);
        bytes_or_rle = ntohs(bytes_or_rle);
        
        size_t bytes = bytes_or_rle * 2;
        std::vector<uint8_t> rle_data(bytes);
        is.read(reinterpret_cast<char*>(rle_data.data()), bytes);
        
        if (has_data) {
            if (!rle_data.empty() && rle_data.back() == 0) rle_data.pop_back();
            uint16_t b = 0;
            for (uint8_t byte : rle_data) {
                if (b >= frame.gate_count) break;
                uint8_t run = byte >> 4;
                uint8_t level = byte & 0x0F;
                for (int i = 0; i < run && b < frame.gate_count; ++i) {
                    frame.data[r * frame.gate_count + b++] = level;
                }
            }
        }
    }
    return has_data;
}

bool RasterPacketHandler::CanHandle(uint16_t packet_code) const {
    return packet_code == 33 || packet_code == 17 || packet_code == 18;
}

bool RasterPacketHandler::Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) {
    uint16_t i_start, j_start, i_scale, j_scale, num_cells, num_rows;
    is.read(reinterpret_cast<char*>(&i_start), 2); 
    is.read(reinterpret_cast<char*>(&j_start), 2);
    is.read(reinterpret_cast<char*>(&i_scale), 2); 
    is.read(reinterpret_cast<char*>(&j_scale), 2);
    is.read(reinterpret_cast<char*>(&num_cells), 2); 
    is.read(reinterpret_cast<char*>(&num_rows), 2);
    
    frame.ray_count = ntohs(num_rows);
    frame.gate_count = ntohs(num_cells);
    frame.gate_spacing = ntohs(i_scale);
    frame.first_gate_dist = 0;
    
    bool has_data = false;
    if (frame.ray_count > 0 && frame.gate_count > 0) {
        frame.data.assign(frame.ray_count * frame.gate_count, 0);
        has_data = true;
    }
    
    for (int r = 0; r < frame.ray_count; ++r) {
        uint16_t bytes_in_row;
        is.read(reinterpret_cast<char*>(&bytes_in_row), 2);
        bytes_in_row = ntohs(bytes_in_row);
        std::vector<uint8_t> row_data(bytes_in_row);
        is.read(reinterpret_cast<char*>(row_data.data()), bytes_in_row);
        
        if (has_data) {
            size_t to_copy = std::min((size_t)frame.gate_count, (size_t)bytes_in_row);
            std::copy(row_data.begin(), row_data.begin() + to_copy, 
                     frame.data.begin() + r * frame.gate_count);
        }
    }
    return has_data;
}

bool RasterDataPacketHandler::CanHandle(uint16_t packet_code) const {
    return packet_code == 0xBA07 || packet_code == 0xBA0F;
}

bool RasterDataPacketHandler::Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) {
    uint16_t op_flag[2], i_start, j_start, x_scale_int, x_scale_frac, y_scale_int, y_scale_frac;
    uint16_t num_rows, packaging_desc;
    
    is.read(reinterpret_cast<char*>(op_flag), 4);
    is.read(reinterpret_cast<char*>(&i_start), 2); 
    is.read(reinterpret_cast<char*>(&j_start), 2);
    is.read(reinterpret_cast<char*>(&x_scale_int), 2); 
    is.read(reinterpret_cast<char*>(&x_scale_frac), 2);
    is.read(reinterpret_cast<char*>(&y_scale_int), 2);
    is.read(reinterpret_cast<char*>(&y_scale_frac), 2);
    is.read(reinterpret_cast<char*>(&num_rows), 2); 
    is.read(reinterpret_cast<char*>(&packaging_desc), 2);
    
    op_flag[0] = ntohs(op_flag[0]);
    op_flag[1] = ntohs(op_flag[1]);
    i_start = ntohs(i_start);
    j_start = ntohs(j_start);
    x_scale_int = ntohs(x_scale_int);
    x_scale_frac = ntohs(x_scale_frac);
    y_scale_int = ntohs(y_scale_int);
    y_scale_frac = ntohs(y_scale_frac);
    num_rows = ntohs(num_rows);
    packaging_desc = ntohs(packaging_desc);
    
    frame.ray_count = num_rows;
    frame.gate_count = 0;
    frame.gate_spacing = x_scale_int;
    frame.first_gate_dist = 0;
    
    bool has_data = false;
    if (frame.ray_count > 0) {
        std::vector<std::vector<uint8_t>> rows(frame.ray_count);
        
        for (int r = 0; r < frame.ray_count; ++r) {
            uint16_t bytes_in_row;
            is.read(reinterpret_cast<char*>(&bytes_in_row), 2);
            bytes_in_row = ntohs(bytes_in_row);
            
            if (bytes_in_row > 0) {
                std::vector<uint8_t> row_data(bytes_in_row);
                is.read(reinterpret_cast<char*>(row_data.data()), bytes_in_row);
                if (row_data.back() == 0) row_data.pop_back();
                rows[r] = row_data;
            }
        }
        
        if (!rows.empty() && !rows[0].empty()) {
            uint16_t bin_count = 0;
            for (uint8_t byte : rows[0]) {
                bin_count += (byte >> 4);
            }
            frame.gate_count = bin_count;
            
            if (frame.gate_count > 0) {
                frame.data.assign(frame.ray_count * frame.gate_count, 0);
                has_data = true;
                
                for (int r = 0; r < frame.ray_count; ++r) {
                    uint16_t b = 0;
                    for (uint8_t byte : rows[r]) {
                        uint8_t run = byte >> 4;
                        uint8_t level = byte & 0x0F;
                        for (int i = 0; i < run && b < frame.gate_count; ++i) {
                            frame.data[r * frame.gate_count + b++] = level;
                        }
                    }
                }
            }
        }
    }
    return has_data;
}

bool GenericPacketHandler::CanHandle(uint16_t packet_code) const {
    return packet_code == 28 || packet_code == 29;
}

bool GenericPacketHandler::Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) {
    uint16_t reserved;
    uint32_t length_of_block;
    
    is.read(reinterpret_cast<char*>(&reserved), 2);
    is.read(reinterpret_cast<char*>(&length_of_block), 4);
    
    reserved = ntohs(reserved);
    length_of_block = ntohl(length_of_block);
    
    if (length_of_block > 0 && length_of_block < 1000000) {
        std::vector<char> data(length_of_block);
        is.read(data.data(), length_of_block);
        return true;
    }
    return false;
}

bool TextSymbolPacketHandler::CanHandle(uint16_t packet_code) const {
    return packet_code == 1 || packet_code == 2 || packet_code == 8;
}

bool TextSymbolPacketHandler::Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) {
    uint16_t text_color;
    is.read(reinterpret_cast<char*>(&text_color), 2);
    
    uint16_t num_strings;
    is.read(reinterpret_cast<char*>(&num_strings), 2);
    num_strings = ntohs(num_strings);
    
    for (int i = 0; i < num_strings; ++i) {
        int16_t i_start, j_start;
        is.read(reinterpret_cast<char*>(&i_start), 2);
        is.read(reinterpret_cast<char*>(&j_start), 2);
        
        uint16_t char_width, char_height;
        is.read(reinterpret_cast<char*>(&char_width), 2);
        is.read(reinterpret_cast<char*>(&char_height), 2);
        
        uint16_t text_length;
        is.read(reinterpret_cast<char*>(&text_length), 2);
        text_length = ntohs(text_length);
        
        std::vector<char> text(text_length);
        is.read(text.data(), text_length);
    }
    return true;
}

bool ContourPacketHandler::CanHandle(uint16_t packet_code) const {
    return packet_code == 0x0E03 || packet_code == 0x3501;
}

bool ContourPacketHandler::Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) {
    uint16_t color_value;
    is.read(reinterpret_cast<char*>(&color_value), 2);
    
    uint16_t num_vectors;
    is.read(reinterpret_cast<char*>(&num_vectors), 2);
    num_vectors = ntohs(num_vectors);
    
    for (int v = 0; v < num_vectors; ++v) {
        int16_t i_start, j_start, i_end, j_end;
        is.read(reinterpret_cast<char*>(&i_start), 2);
        is.read(reinterpret_cast<char*>(&j_start), 2);
        is.read(reinterpret_cast<char*>(&i_end), 2);
        is.read(reinterpret_cast<char*>(&j_end), 2);
    }
    return true;
}

bool GenericBytePacketHandler::CanHandle(uint16_t packet_code) const {
    return true;
}

bool GenericBytePacketHandler::Parse(std::istream& is, const PacketContext& ctx, RadarFrame& frame) {
    std::streampos start = is.tellg();
    uint16_t reserved;
    uint32_t length_of_block;
    
    if (!is.read(reinterpret_cast<char*>(&reserved), 2)) return false;
    if (!is.read(reinterpret_cast<char*>(&length_of_block), 4)) return false;
    
    length_of_block = ntohl(length_of_block);
    
    if (length_of_block > 0 && length_of_block < 10000000) {
        std::vector<char> data(length_of_block);
        if (!is.read(data.data(), length_of_block)) {
            is.seekg(start);
            return false;
        }
        return true;
    }
    is.seekg(start);
    return false;
}

PacketHandlerRegistry& PacketHandlerRegistry::Instance() {
    static PacketHandlerRegistry instance;
    return instance;
}

PacketHandlerRegistry::PacketHandlerRegistry() {
    handlers_.push_back(std::make_shared<RadialPacketHandler>());
    handlers_.push_back(std::make_shared<RleRadialPacketHandler>());
    handlers_.push_back(std::make_shared<RasterDataPacketHandler>());
    handlers_.push_back(std::make_shared<RasterPacketHandler>());
    handlers_.push_back(std::make_shared<GenericPacketHandler>());
    handlers_.push_back(std::make_shared<TextSymbolPacketHandler>());
    handlers_.push_back(std::make_shared<ContourPacketHandler>());
    handlers_.push_back(std::make_shared<GenericBytePacketHandler>());
}

std::shared_ptr<PacketHandler> PacketHandlerRegistry::GetHandler(uint16_t packet_code) {
    for (auto& handler : handlers_) {
        if (handler->CanHandle(packet_code)) {
            return handler;
        }
    }
    return nullptr;
}

bool PacketHandlerRegistry::ParsePacket(std::istream& is, uint16_t packet_code,
                                       const PacketContext& ctx, RadarFrame& frame) {
    auto handler = GetHandler(packet_code);
    if (handler) {
        return handler->Parse(is, ctx, frame);
    }
    return false;
}

} // namespace leveliii
