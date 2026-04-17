#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace leveliii {

enum class PacketCode : uint16_t {
    TextNoValue = 1,
    SpecialSymbol = 2,
    MesocycloneSymbol3 = 3,
    WindBarbData = 4,
    VectorArrowData = 5,
    LinkedVectorNoValue = 6,
    UnlinkedVectorNoValue = 7,
    TextUniform = 8,
    LinkedVectorUniform = 9,
    UnlinkedVectorUniform = 10,
    MesocycloneSymbol11 = 11,
    TornadoVortexSignatureSymbol = 12,
    HailPositiveSymbol = 13,
    HailProbableSymbol = 14,
    StormId = 15,
    DigitalRadialDataArray = 16,
    DigitalPrecipitationDataArray = 17,
    PrecipitationRateDataArray = 18,
    HdaHailSymbol = 19,
    PointFeatureSymbol = 20,
    CellTrendData = 21,
    CellTrendVolumeScanTimes = 22,
    ScitPastData = 23,
    ScitForecastData = 24,
    StiCircle = 25,
    ElevatedTornadoVortexSignatureSymbol = 26,
    GenericData28 = 28,
    GenericData29 = 29,
    DigitalRasterDataArray = 33,
    SetColorLevel = 0x0802,
    LinkedContourVector = 0x0E03,
    UnlinkedContourVector = 0x3501,
    MapMessage0E23 = 0x0E23,
    MapMessage3521 = 0x3521,
    MapMessage4E00 = 0x4E00,
    MapMessage4E01 = 0x4E01,
    RadialData = 0xAF1F,
    RasterDataBA07 = 0xBA07,
    RasterDataBA0F = 0xBA0F
};

enum class ProductCode : int16_t {
    N0R = 19,
    N0V = 27,
    SW = 30,
    NCR = 37,
    NET = 41,
    SRM = 56,
    NVL = 57,
    N1P = 78,
    N3P = 79,
    NTP = 80,
    DPA = 81,
    SPD = 82,
    N0Q = 94,
    DR = 94,
    DV = 99,
    N0U = 99,
    N1Q = 110,
    N2Q = 111,
    DVL = 134,
    EET = 135,
    DSP = 138,
    SDR = 153,
    SDV = 154,
    DZD = 159,
    DCC = 161,
    DKD = 163,
    DHC = 165,
    ML = 166,
    OHA = 169,
    DAA = 170,
    DTA = 172,
    DUA = 173,
    DOD = 174,
    DSD = 175,
    HHC = 177,
    TDR = 180,
    TDV = 182,
    TZL = 186,
    Unknown = -1
};

struct WmoHeader {
    std::string sequence_number;
    std::string data_type;
    std::string geographic_designator;
    std::string bulletin_id;
    std::string icao;
    std::string date_time;
    std::string bbb_indicator;
    std::string product_category;
    std::string product_designator;
};

struct CcbHeader {
    uint16_t ff;
    uint16_t ccb_length;
    uint8_t mode;
    uint8_t submode;
    char precedence;
    char classification;
    std::string message_originator;
    uint8_t category;
    uint8_t subcategory;
    uint16_t user_defined;
    uint8_t year;
    uint8_t month;
    uint8_t tor_day;
    uint8_t tor_hour;
    uint8_t tor_minute;
    uint8_t numberOfDestinations;
    std::vector<std::string> message_destination;
};

#pragma pack(push, 1)
struct MessageHeader {
    int16_t message_code;
    uint16_t date_of_message;
    uint32_t time_of_message;
    uint32_t length_of_message;
    uint16_t source_id;
    uint16_t destination_id;
    uint16_t number_blocks;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ProductDescriptionBlock {
    int16_t block_divider;           // HW 10
    int32_t latitude_raw;            // HW 11-12
    int32_t longitude_raw;           // HW 13-14
    int16_t height;                  // HW 15
    int16_t product_code;            // HW 16
    uint16_t operational_mode;       // HW 17
    uint16_t vcp;                    // HW 18
    int16_t sequence_number;         // HW 19
    uint16_t volume_scan_number;     // HW 20
    uint16_t volume_scan_date;       // HW 21
    uint32_t volume_scan_start_time; // HW 22-23
    uint16_t generation_date;        // HW 24
    uint32_t generation_time;        // HW 25-26
    uint16_t product_dependent_27;    // HW 27
    uint16_t product_dependent_28;    // HW 28
    uint16_t elevation_number;       // HW 29
    uint16_t product_dependent_30;   // HW 30
    uint16_t data_level_thresholds[16]; // HW 31-46
    uint16_t product_dependent_47;    // HW 47
    uint16_t product_dependent_48;    // HW 48
    uint16_t product_dependent_49;    // HW 49
    uint16_t product_dependent_50;    // HW 50
    uint16_t product_dependent_51;    // HW 51
    uint16_t product_dependent_52;    // HW 52
    uint16_t product_dependent_53;    // HW 53
    uint8_t version;                 // HW 54 (high byte)
    uint8_t spot_blank;              // HW 54 (low byte)
    uint32_t offset_to_symbology;    // HW 55-56
    uint32_t offset_to_graphic;      // HW 57-58
    uint32_t offset_to_tabular;      // HW 59-60

    // Decoded fields (not packed)
    float latitude;
    float longitude;
};
#pragma pack(pop)

struct SymbologyLayer {
    int16_t layer_divider;
    uint32_t length_of_layer;
    std::vector<uint8_t> data; // Raw data of packets in this layer
};

struct SymbologyBlock {
    int16_t block_divider;
    int16_t block_id;
    uint32_t length_of_block;
    uint16_t number_of_layers;
    std::vector<SymbologyLayer> layers;
};

struct Radial {
    uint16_t start_angle;
    uint16_t delta_angle;
    std::vector<uint8_t> data;
};

struct RadialDataPacket {
    uint16_t packet_code;
    uint16_t first_range_bin;
    uint16_t num_range_bins;
    int16_t center_i;
    int16_t center_j;
    uint16_t range_scale;
    uint16_t num_radials;
    std::vector<Radial> radials;
};

struct RasterDataPacket {
    uint16_t packet_code;
    // ... Simplified for now
    std::vector<uint8_t> data;
};

struct RadarFrame {
    std::string station_id;
    int16_t product_code;
    std::string product_name;
    std::string timestamp;
    float elevation;
    uint16_t ray_count;
    uint16_t gate_count;
    float gate_spacing;
    float first_gate_dist;
    float latitude;
    float longitude;
    int16_t height;
    std::vector<uint8_t> data; // ray_count * gate_count
};

const char* GetProductName(int16_t code);
const char* GetProductCategory(int16_t code);
const char* GetProductStorageCategory(int16_t code);

} // namespace leveliii
