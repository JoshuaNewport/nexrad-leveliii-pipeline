#include "LevelIII_Parser.h"
#include "leveliii/PacketHandlers.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#include <cstring>
#include <ctime>
#include <time.h>
#include <zlib.h>
#include <bzlib.h>
#include <algorithm>
#include <iostream>

#ifdef _WIN32
#   include <WinSock2.h>
#else
#   include <arpa/inet.h>
#endif

namespace leveliii {

static bool get_line_ascii(std::istream& is, std::string& line) {
    line.clear();
    char c;
    while (is.get(c)) {
        if (static_cast<unsigned char>(c) == 0x03) { // ETX
            is.seekg(-1, std::ios::cur);
            return !line.empty();
        }
        if (c == '\r') {
            while (is.peek() == '\r') is.get();
            if (is.peek() == '\n') is.get();
            return true;
        }
        if (c == '\n') return true;
        
        // Strictly ASCII
        if (static_cast<unsigned char>(c) < 32 && c != '\t' && c != 0x01) {
            is.seekg(-1, std::ios::cur);
            return !line.empty();
        }
        line += c;
        if (line.length() > 256) break; // sanity
    }
    return !line.empty();
}

Parser::Parser() : compressed_(false) {
    std::memset(&message_header_, 0, sizeof(message_header_));
    std::memset(&pdb_, 0, sizeof(pdb_));
    std::memset(&ccb_header_, 0, sizeof(ccb_header_));
}

Parser::~Parser() {}

bool Parser::LoadFile(const std::string& filename) {
    std::ifstream is(filename, std::ios::binary);
    if (!is.is_open()) return false;
    return LoadData(is);
}

bool Parser::LoadData(std::istream& is) {
    // Clear previous parsing state
    symbology_.layers.clear();
    
    // 1. Initial WMO Header
    ParseWmoHeader(is, wmo_header_);

    // 2. Decompression (ZLIB/GZIP)
    std::stringstream decompressed_stream;
    bool found_compression = false;
    while (is.peek() == 0x78 || is.peek() == 0x1f) {
        found_compression = true;
        if (!Decompress(is, decompressed_stream)) break;
    }

    std::string data;
    if (found_compression) {
        data = decompressed_stream.str();
    } else {
        std::stringstream ss;
        ss << is.rdbuf();
        data = ss.str();
    }

    if (data.empty()) return false;

    // 3. Search for Message Anchor (0xFFFF divider at offset 18 from start of message header)
    size_t divider_pos = std::string::npos;
    for (size_t i = 18; i < data.size() - 14; ++i) {
        if (static_cast<uint8_t>(data[i]) == 0xFF && static_cast<uint8_t>(data[i+1]) == 0xFF) {
            // Verify message header 18 bytes prior
            uint16_t msg_code_raw;
            std::memcpy(&msg_code_raw, &data[i-18], 2);
            uint16_t msg_code = ntohs(msg_code_raw);
            
            // PDB is at offset 18 from message header.
            // product_code in PDB is at offset 12 from start of PDB (i.e. divider_pos + 12)
            uint16_t pdb_prod_code_raw;
            std::memcpy(&pdb_prod_code_raw, &data[i+12], 2);
            uint16_t pdb_prod_code = ntohs(pdb_prod_code_raw);
            
            if (msg_code == pdb_prod_code && msg_code > 0 && msg_code < 300) {
                divider_pos = i;
                break;
            }
        }
    }

    if (divider_pos == std::string::npos) {
        // Fallback: try to find the divider without code matching
        divider_pos = data.find("\xFF\xFF");
        if (divider_pos == std::string::npos || divider_pos < 18) return false;
    }

    std::stringstream message_ss(data.substr(divider_pos - 18));
    
    if (!ParseMessageHeader(message_ss)) return false;
    if (!ParseProductDescriptionBlock(message_ss)) return false;
    
    if (IsCompressionEnabled()) {
        size_t message_len = message_header_.length_of_message;
        size_t prefix_len = 18 + 102; // Message Header (18) + PDB (102 bytes)
        size_t compressed_size = (message_len > prefix_len) ? message_len - prefix_len : 0;
        
        std::stringstream bzip2_decompressed;
        if (DecompressBzip2(message_ss, bzip2_decompressed, compressed_size)) {
            // Respect the symbology offset even in compressed stream
            if (pdb_.offset_to_symbology > 0) {
                size_t offsetBase = 18 + 102; // Message Header (18) + PDB (102)
                if (pdb_.offset_to_symbology > offsetBase) {
                    bzip2_decompressed.seekg(pdb_.offset_to_symbology - offsetBase, std::ios::beg);
                }
            }
            if (ParseSymbologyBlock(bzip2_decompressed)) {
                return true;
            }
        }
        
        // BZIP2 failed or symbology parsing failed - try uncompressed
        message_ss.clear();
        message_ss.seekg(0, std::ios::beg);
        // Skip past message header and PDB
        message_ss.seekg(18 + 102, std::ios::beg);
        
        if (pdb_.offset_to_symbology > 0) {
            message_ss.seekg(pdb_.offset_to_symbology, std::ios::beg);
        }
        if (ParseSymbologyBlock(message_ss)) {
            return true;
        }
        
        // Even if symbology parsing fails, we have valid header/PDB data
        return true;
    } else {
        // Offset to symbology is from start of RPG message (message header)
        if (pdb_.offset_to_symbology > 0) {
            message_ss.seekg(pdb_.offset_to_symbology, std::ios::beg);
        }
        if (ParseSymbologyBlock(message_ss)) {
            return true;
        }
        
        // Even if symbology parsing fails, we have valid header/PDB data
        return true;
    }
}

bool Parser::ParseWmoHeader(std::istream& is, WmoHeader& header) {
    bool foundStart = false;
    while (is.peek() != EOF) {
        int c = is.peek();
        if (c == 0x01 || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) {
            foundStart = true;
            break;
        }
        is.get();
    }
    
    if (!foundStart) return false;

    std::string sohLine, sequenceLine, wmoLine, awipsLine;
    std::streampos headerStart = is.tellg();

    if (is.peek() == 0x01) {
        get_line_ascii(is, sohLine);
        if (sohLine.length() <= 1) {
            get_line_ascii(is, sequenceLine);
            get_line_ascii(is, wmoLine);
        } else {
            sequenceLine = sohLine.substr(1);
            get_line_ascii(is, wmoLine);
        }
    } else {
        get_line_ascii(is, wmoLine);
        if (wmoLine.length() < 18) {
            sequenceLine = wmoLine;
            get_line_ascii(is, wmoLine);
        }
    }

    if (wmoLine.length() < 18) {
        is.seekg(headerStart, std::ios::beg);
        return false;
    }
    
    std::stringstream wmoTokens(wmoLine);
    std::vector<std::string> tokens;
    std::string token;
    while (wmoTokens >> token) tokens.push_back(token);

    if (tokens.size() < 3) {
        is.seekg(headerStart, std::ios::beg);
        return false;
    }

    header.sequence_number = sequenceLine;
    header.data_type = tokens[0].substr(0, 2);
    header.icao = tokens[1];
    header.date_time = tokens[2];
    if (tokens.size() > 3) header.bbb_indicator = tokens[3];

    std::streamoff pos = is.tellg();
    if (get_line_ascii(is, awipsLine)) {
        if (awipsLine.length() == 6) {
            header.product_category = awipsLine.substr(0, 3);
            header.product_designator = awipsLine.substr(3, 3);
        } else {
            is.seekg(pos, std::ios::beg);
        }
    }
    return true;
}

bool Parser::ParseCcbHeader(std::istream& is) {
    uint16_t ccb_len_raw;
    std::streampos start = is.tellg();
    if (!is.read(reinterpret_cast<char*>(&ccb_len_raw), 2)) return false;
    
    ccb_len_raw = ntohs(ccb_len_raw);
    ccb_header_.ccb_length = ccb_len_raw & 0x3FFF;
    ccb_header_.ff = ccb_len_raw >> 14;

    if (ccb_header_.ccb_length < 2 || ccb_header_.ccb_length > 512) {
        is.seekg(start, std::ios::beg);
        return false;
    }

    is.seekg(start + static_cast<std::streamoff>(ccb_header_.ccb_length), std::ios::beg);
    return !is.fail();
}

bool Parser::ParseMessageHeader(std::istream& is) {
    if (!is.read(reinterpret_cast<char*>(&message_header_), 18)) return false;

    message_header_.message_code = ntohs(message_header_.message_code);
    message_header_.date_of_message = ntohs(message_header_.date_of_message);
    message_header_.time_of_message = ntohl(message_header_.time_of_message);
    message_header_.length_of_message = ntohl(message_header_.length_of_message);
    message_header_.source_id = ntohs(message_header_.source_id);
    message_header_.destination_id = ntohs(message_header_.destination_id);
    message_header_.number_blocks = ntohs(message_header_.number_blocks);

    return (message_header_.message_code >= 0 && message_header_.message_code < 500);
}

bool Parser::ParseProductDescriptionBlock(std::istream& is) {
    // ProductDescriptionBlock is 102 halfwords (HW) = 204 bytes in the full ICD
    // But the RPG message format only includes HW 10-60 (102 bytes)
    if (!is.read(reinterpret_cast<char*>(&pdb_), 102)) return false;

    pdb_.block_divider = ntohs(pdb_.block_divider);

    pdb_.latitude_raw = ntohl(pdb_.latitude_raw);
    pdb_.longitude_raw = ntohl(pdb_.longitude_raw);
    pdb_.latitude = pdb_.latitude_raw / 1000.0f;
    pdb_.longitude = pdb_.longitude_raw / 1000.0f;

    pdb_.height = ntohs(pdb_.height);
    pdb_.product_code = ntohs(pdb_.product_code);
    pdb_.operational_mode = ntohs(pdb_.operational_mode);
    pdb_.vcp = ntohs(pdb_.vcp);
    pdb_.sequence_number = ntohs(pdb_.sequence_number);
    pdb_.volume_scan_number = ntohs(pdb_.volume_scan_number);
    pdb_.volume_scan_date = ntohs(pdb_.volume_scan_date);
    pdb_.volume_scan_start_time = ntohl(pdb_.volume_scan_start_time);
    pdb_.generation_date = ntohs(pdb_.generation_date);
    pdb_.generation_time = ntohl(pdb_.generation_time);
    pdb_.product_dependent_27 = ntohs(pdb_.product_dependent_27);
    pdb_.product_dependent_28 = ntohs(pdb_.product_dependent_28);
    pdb_.elevation_number = ntohs(pdb_.elevation_number);
    pdb_.product_dependent_30 = ntohs(pdb_.product_dependent_30);

    for (int i = 0; i < 16; ++i) {
        pdb_.data_level_thresholds[i] = ntohs(pdb_.data_level_thresholds[i]);
    }
    
    pdb_.product_dependent_47 = ntohs(pdb_.product_dependent_47);
    pdb_.product_dependent_48 = ntohs(pdb_.product_dependent_48);
    pdb_.product_dependent_49 = ntohs(pdb_.product_dependent_49);
    pdb_.product_dependent_50 = ntohs(pdb_.product_dependent_50);
    pdb_.product_dependent_51 = ntohs(pdb_.product_dependent_51);
    pdb_.product_dependent_52 = ntohs(pdb_.product_dependent_52);
    pdb_.product_dependent_53 = ntohs(pdb_.product_dependent_53);

    pdb_.offset_to_symbology = ntohl(pdb_.offset_to_symbology) * 2;
    pdb_.offset_to_graphic = ntohl(pdb_.offset_to_graphic) * 2;
    pdb_.offset_to_tabular = ntohl(pdb_.offset_to_tabular) * 2;

    return true;
}

bool Parser::ParseSymbologyBlock(std::istream& is) {
    if (!is.read(reinterpret_cast<char*>(&symbology_.block_divider), 2)) return false;
    is.read(reinterpret_cast<char*>(&symbology_.block_id), 2);
    is.read(reinterpret_cast<char*>(&symbology_.length_of_block), 4);
    is.read(reinterpret_cast<char*>(&symbology_.number_of_layers), 2);

    symbology_.block_divider = ntohs(symbology_.block_divider);
    symbology_.block_id = ntohs(symbology_.block_id);
    symbology_.length_of_block = ntohl(symbology_.length_of_block);
    symbology_.number_of_layers = ntohs(symbology_.number_of_layers);

    if (pdb_.product_code == 135) {
        std::cerr << "[EET ParseSymbologyBlock] divider=" << std::hex << symbology_.block_divider 
                  << " id=" << symbology_.block_id << " len=" << std::dec << symbology_.length_of_block
                  << " layers=" << symbology_.number_of_layers << std::endl;
    }

    if (symbology_.block_divider != -1) return false;

    symbology_.layers.clear();
    for (uint16_t i = 0; i < symbology_.number_of_layers; ++i) {
        SymbologyLayer layer;
        if (!is.read(reinterpret_cast<char*>(&layer.layer_divider), 2)) break;
        if (!is.read(reinterpret_cast<char*>(&layer.length_of_layer), 4)) break;
        
        layer.layer_divider = ntohs(layer.layer_divider);
        layer.length_of_layer = ntohl(layer.length_of_layer);
        
        if (layer.length_of_layer == 0 || layer.length_of_layer > 10000000) {
            return symbology_.layers.size() > 0;
        }
        
        layer.data.resize(layer.length_of_layer);
        if (!is.read(reinterpret_cast<char*>(layer.data.data()), layer.length_of_layer)) {
            return symbology_.layers.size() > 0;
        }
        
        symbology_.layers.push_back(std::move(layer));
    }

    return symbology_.layers.size() > 0;
}

RadarFrame Parser::ExtractRadarFrame() const {
    RadarFrame frame;
    frame.station_id = wmo_header_.icao;
    frame.product_code = pdb_.product_code;
    frame.timestamp = FormatTimestamp(pdb_.volume_scan_date, pdb_.volume_scan_start_time);
    frame.elevation = (int16_t)pdb_.product_dependent_30 * 0.1f;
    frame.latitude = pdb_.latitude;
    frame.longitude = pdb_.longitude;
    frame.height = pdb_.height;
    
    frame.ray_count = 0;
    frame.gate_count = 0;
    frame.gate_spacing = 0.0f;
    frame.first_gate_dist = 0.0f;

    if (symbology_.layers.empty()) {
        return frame;
    }

    PacketContext ctx{pdb_.product_code, pdb_};
    auto& registry = PacketHandlerRegistry::Instance();
    
    for (const auto& layer : symbology_.layers) {
        std::string layer_str(layer.data.begin(), layer.data.end());
        std::stringstream ss;
        ss.rdbuf()->pubsetbuf(const_cast<char*>(layer_str.data()), layer_str.size());
        ss.seekg(0);
        
        while (ss.peek() != EOF) {
            uint16_t packet_code;
            if (!ss.read(reinterpret_cast<char*>(&packet_code), 2)) break;
            packet_code = ntohs(packet_code);

            registry.ParsePacket(ss, packet_code, ctx, frame);
            
            if (frame.ray_count > 0 && frame.gate_count > 0) {
                return frame;
            }
        }
    }
    return frame;
}

bool Parser::Decompress(std::istream& is, std::ostream& os) {
    z_stream strm;
    strm.zalloc = Z_NULL; strm.zfree = Z_NULL; strm.opaque = Z_NULL;
    strm.avail_in = 0; strm.next_in = Z_NULL;
    int ret = inflateInit2(&strm, 15 + 32);
    if (ret != Z_OK) return false;
    const size_t IN_CHUNK = 16384, OUT_CHUNK = 65536;
    unsigned char in[IN_CHUNK], out[OUT_CHUNK];
    do {
        is.read(reinterpret_cast<char*>(in), IN_CHUNK);
        strm.avail_in = (uInt)is.gcount();
        if (strm.avail_in == 0) break;
        strm.next_in = in;
        do {
            strm.avail_out = OUT_CHUNK; strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            if (ret < 0 && ret != Z_BUF_ERROR) { inflateEnd(&strm); return false; }
            os.write(reinterpret_cast<char*>(out), OUT_CHUNK - strm.avail_out);
        } while (strm.avail_out == 0);
        if (ret == Z_STREAM_END && !is.eof()) {
            int next;
            while ((next = is.peek()) != EOF && next != 0x78 && next != 0x1f) is.get();
            if (is.peek() != EOF) { inflateReset(&strm); ret = Z_OK; }
        }
    } while (ret != Z_STREAM_END && !is.eof());
    inflateEnd(&strm);
    return ret == Z_STREAM_END || ret == Z_OK;
}

bool Parser::IsCompressionEnabled() const {
    static const std::set<int> compressedProducts = {
       32,  94,  99,  113, 134, 135, 138, 149, 152, 153, 154, 155, 159,
       161, 163, 165, 167, 168, 170, 172, 173, 174, 175, 176, 177, 178,
       179, 180, 182, 186, 189, 190, 191, 192, 193, 195, 197, 202};
    if (compressedProducts.find(pdb_.product_code) != compressedProducts.end()) {
        // ICD Table V: HW 51 is the Compression Indicator for modern digital products
        return (pdb_.product_dependent_51 == 1);
    }
    return false;
}

bool Parser::DecompressBzip2(std::istream& is, std::ostream& os, size_t compressed_size) {
    if (compressed_size == 0) return true;
    std::vector<char> compressed_data(compressed_size);
    if (!is.read(compressed_data.data(), compressed_size)) return false;
    
    bz_stream strm;
    strm.bzalloc = NULL; strm.bzfree = NULL; strm.opaque = NULL;
    strm.next_in = compressed_data.data(); strm.avail_in = (unsigned int)compressed_size;
    int ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK) return false;
    
    const size_t OUT_CHUNK = 65536;
    char out[OUT_CHUNK];
    do {
        strm.next_out = out; strm.avail_out = OUT_CHUNK;
        ret = BZ2_bzDecompress(&strm);
        if (ret != BZ_OK && ret != BZ_STREAM_END) {
            BZ2_bzDecompressEnd(&strm);
            return false;
        }
        os.write(out, OUT_CHUNK - strm.avail_out);
    } while (ret != BZ_STREAM_END);
    BZ2_bzDecompressEnd(&strm);
    return true;
}

std::string Parser::FormatTimestamp(uint16_t volume_scan_date, uint32_t volume_scan_start_time) const {
    static const std::time_t NEXRAD_EPOCH_SECONDS = -86400;
    
    std::time_t days_since_epoch = static_cast<std::time_t>(volume_scan_date);
    std::time_t milliseconds = static_cast<std::time_t>(volume_scan_start_time);
    
    std::time_t epoch_seconds = NEXRAD_EPOCH_SECONDS + (days_since_epoch * 86400) + (milliseconds / 1000);
    
    struct tm* tm_info = std::gmtime(&epoch_seconds);
    
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", tm_info);
    
    return std::string(buffer);
}

} // namespace leveliii
