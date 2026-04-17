#pragma once

#include "LevelIII_Types.h"
#include <iostream>
#include <memory>
#include <optional>

namespace leveliii {

class Parser {
public:
    Parser();
    ~Parser();

    bool LoadFile(const std::string& filename);
    bool LoadData(std::istream& is);

    const WmoHeader& wmo_header() const { return wmo_header_; }
    const CcbHeader& ccb_header() const { return ccb_header_; }
    const WmoHeader& inner_wmo_header() const { return inner_wmo_header_; }
    const MessageHeader& message_header() const { return message_header_; }
    const ProductDescriptionBlock& product_description_block() const { return pdb_; }
    const SymbologyBlock& symbology_block() const { return symbology_; }

    RadarFrame ExtractRadarFrame() const;

    bool is_compressed() const { return compressed_; }

private:
    bool ParseWmoHeader(std::istream& is, WmoHeader& header);
    bool ParseCcbHeader(std::istream& is);
    bool ParseMessageHeader(std::istream& is);
    bool ParseProductDescriptionBlock(std::istream& is);
    bool ParseSymbologyBlock(std::istream& is);
    bool FinishParsing(std::istream& is, std::streampos message_start);
    bool Decompress(std::istream& is, std::ostream& os);
    bool DecompressBzip2(std::istream& is, std::ostream& os, size_t compressed_size);
    bool IsCompressionEnabled() const;
    std::string FormatTimestamp(uint16_t volume_scan_date, uint32_t volume_scan_start_time) const;

    WmoHeader wmo_header_;
    CcbHeader ccb_header_;
    WmoHeader inner_wmo_header_;
    MessageHeader message_header_;
    ProductDescriptionBlock pdb_;
    SymbologyBlock symbology_;
    bool compressed_ = false;
};

} // namespace leveliii
