#include "leveliii/ProductDatabase.h"

namespace leveliii {

ProductDatabase& ProductDatabase::Instance() {
    static ProductDatabase instance;
    return instance;
}

ProductDatabase::ProductDatabase() {
    InitializeProducts();
    BuildAwipsLookup();
}

void ProductDatabase::InitializeProducts() {
    auto add_product = [this](int16_t code, const char* name, const char* desc, 
                              const std::vector<const char*>& awips, const char* cat, const char* storage_cat,
                              bool compressed, const std::vector<uint16_t>& packets) {
        ProductInfo info;
        info.code = code;
        info.name = name;
        info.description = desc;
        info.category = cat;
        info.storage_category = storage_cat;
        info.is_compressed = compressed;
        
        for (int i = 0; i < 9; ++i) {
            info.awips_names[i] = (i < (int)awips.size()) ? awips[i] : "";
        }
        
        for (int i = 0; i < 8; ++i) {
            info.supported_packet_types[i] = (i < (int)packets.size()) ? packets[i] : 0;
        }
        
        products_[code] = info;
    };
    
    add_product(19, "N0R", "Base Reflectivity", {"N0R"}, "reflectivity", "reflectivity", false, {16});
    add_product(27, "N0V", "Base Velocity", {"N0V"}, "velocity", "velocity", false, {16});
    add_product(30, "SW", "Spectrum Width", {"NSW"}, "spectrum_width", "spectrum_width", false, {16});
    add_product(37, "NCR", "Composite Reflectivity", {"NCR"}, "reflectivity", "reflectivity", false, {0xBA07});
    add_product(41, "NET", "Echo Tops", {"NET"}, "echo_tops", "enhanced_echo_tops", false, {16});
    add_product(56, "SRM", "Storm Relative Mean Radial Velocity", {"N0S", "N1S", "N2S", "N3S"}, "velocity", "velocity", false, {16});
    add_product(57, "NVL", "Vertically Integrated Liquid", {"NVL"}, "vil", "vil", false, {0xBA07});
    add_product(78, "N1P", "1-hour Rainfall Accumulation", {"N1P"}, "precipitation", "precipitation", true, {0xBA07});
    add_product(79, "N3P", "3-hour Rainfall Accumulation", {"N3P"}, "precipitation", "precipitation", true, {0xBA07});
    add_product(80, "NTP", "Storm Total Rainfall Accumulation", {"NTP"}, "precipitation", "precipitation", true, {0xBA07});
    add_product(81, "DPA", "Digital Precipitation Array", {"DPA"}, "precipitation", "precipitation", true, {17});
    add_product(82, "SPD", "Supplemental Precipitation Data", {"SPD"}, "precipitation", "precipitation", false, {33});
    add_product(94, "DR", "Digital Reflectivity", {"N0Q", "N1Q", "N2Q", "NAQ", "NBQ"}, "reflectivity", "reflectivity", true, {16, 0xAF1F});
    add_product(99, "DV", "Digital Velocity", {"N0U", "N1U", "N2U", "N3U"}, "velocity", "velocity", true, {16, 0xAF1F});
    add_product(110, "N1Q", "Base Reflectivity Layer 2", {"N1Q"}, "reflectivity", "reflectivity", false, {16, 0xAF1F});
    add_product(111, "N2Q", "Base Reflectivity Layer 3", {"N2Q"}, "reflectivity", "reflectivity", false, {16, 0xAF1F});
    add_product(134, "DVL", "Digital Vertically Integrated Liquid", {"DVL"}, "vil", "vil", true, {16, 0xAF1F});
    //--=================--
    //--=== Echo Tops ===--
    //--=================--
    add_product(135, "EET", "Enhanced Echo Tops", {"EET"}, "echo_tops", "enhanced_echo_tops", true, {16, 0xAF1F});
    add_product(138, "DSP", "Digital Storm Total Precipitation", {"DSP"}, "precipitation", "precipitation", true, {28, 29});
    //--================--
    //--=== Super Res===--
    //--================--
    add_product(153, "SDR", "Super-Resolution Reflectivity", {"NXB", "NYB", "NZB", "N0B", "NAB", "N1B", "NBB", "N2B", "N3B"}, "super_res", "super_res_reflectivity", true, {16, 0xAF1F});
    add_product(154, "SDV", "Super-Resolution Velocity", {"NXG", "NYG", "NZG", "N0G", "NAG", "N1G", "NBU", "N2U", "N3U"}, "super_res", "super_res_velocity", true, {16, 0xAF1F});
    //--====================--
    //--=== Digital Data ===--
    //--====================--
    add_product(159, "DZD", "Digital Differential Reflectivity", {"NXX","NYX","NZX","N0X","NAX","N1X","NBX","N2X","N3X"}, "differential_reflectivity", "differential_reflectivity", true, {16, 0xAF1F});
    add_product(161, "DCC", "Digital Correlation Coefficient", {"NXC","NYC","NZC","N0C","NAC","N1C","NBC","N2C","N3C"}, "correlation_coefficient", "correlation_coefficient", true, {16, 0xAF1F});
    add_product(163, "DKD", "Digital Specific Differential Phase", {"N0K", "N1K", "N2K", "N3K"}, "specific_differential_phase", "specific_differential_phase", true, {16, 0xAF1F});
    add_product(165, "DHC", "Digital Hydrometeor Classification", {"N0H", "N1H"}, "hydrometeor_classification", "hydrometeor_classification", true, {1, 2, 8});
    add_product(166, "ML", "Melting Layer", {"ML"}, "melting_layer", "melting_layer", false, {1, 2, 8});
    add_product(169, "OHA", "One Hour Accumulation", {"OHA"}, "precipitation", "precipitation", true, {1, 2, 8});
    add_product(170, "DAA", "Digital Accumulation Array", {"DAA"}, "precipitation", "precipitation", true, {1, 2, 8});
    add_product(172, "DTA", "Digital Storm Total Accumulation", {"DTA"}, "precipitation", "precipitation", true, {1, 2, 8});
    add_product(173, "DUA", "Digital User-Selectable Accumulation", {"DU3", "DU6"}, "precipitation", "precipitation", true, {0xBA07});
    add_product(174, "DOD", "Digital One-Hour Difference Accumulation", {"DOD"}, "precipitation", "precipitation", true, {0xBA07});
    add_product(175, "DSD", "Digital Storm Total Difference Accumulation", {"DSD"}, "precipitation", "precipitation", true, {0xBA07});
    add_product(177, "HHC", "Hybrid Hydrometeor Classification", {"HHC"}, "hydrometeor_classification", "hydrometeor_classification", true, {16, 0xAF1F});
    add_product(180, "TDR", "Digital Reflectivity (Long Range)", {"TZ0", "TZ1", "TZ2"}, "reflectivity", "reflectivity", true, {16, 0xAF1F});
    add_product(182, "TDV", "Digital Velocity (Long Range)", {"TV0", "TV1", "TV2"}, "velocity", "velocity", true, {16, 0xAF1F});
    add_product(186, "TZL", "Long Range Reflectivity", {"TZL"}, "reflectivity", "reflectivity", false, {16, 0xAF1F});
}

void ProductDatabase::BuildAwipsLookup() {
    for (const auto& pair : products_) {
        const ProductInfo& info = pair.second;
        for (int i = 0; i < 9; ++i) {
            if (info.awips_names[i] && info.awips_names[i][0] != '\0') {
                awips_to_code_[info.awips_names[i]] = info.code;
            }
        }
    }
}

const ProductInfo* ProductDatabase::GetProductInfo(int16_t code) const {
    auto it = products_.find(code);
    if (it != products_.end()) {
        return &it->second;
    }
    return nullptr;
}

const ProductInfo* ProductDatabase::FindByAwipsName(const std::string& awips_name) const {
    auto it = awips_to_code_.find(awips_name);
    if (it != awips_to_code_.end()) {
        return GetProductInfo(it->second);
    }
    return nullptr;
}

const char* ProductDatabase::GetProductName(int16_t code) const {
    const auto* info = GetProductInfo(code);
    return info ? info->name : "Unknown";
}

const char* ProductDatabase::GetProductDescription(int16_t code) const {
    const auto* info = GetProductInfo(code);
    return info ? info->description : "Unknown product";
}

const char* ProductDatabase::GetProductCategory(int16_t code) const {
    const auto* info = GetProductInfo(code);
    return info ? info->category : "unknown";
}

const char* ProductDatabase::GetProductStorageCategory(int16_t code) const {
    const auto* info = GetProductInfo(code);
    return info ? info->storage_category : "unknown";
}

bool ProductDatabase::IsCompressed(int16_t code) const {
    const auto* info = GetProductInfo(code);
    return info ? info->is_compressed : false;
}

bool ProductDatabase::SupportsPacketType(int16_t product_code, uint16_t packet_code) const {
    const auto* info = GetProductInfo(product_code);
    if (!info) return false;
    
    for (int i = 0; i < 8; ++i) {
        if (info->supported_packet_types[i] == 0) break;
        if (info->supported_packet_types[i] == packet_code) return true;
    }
    return false;
}

std::vector<int16_t> ProductDatabase::GetAllProductCodes() const {
    std::vector<int16_t> codes;
    for (const auto& pair : products_) {
        codes.push_back(pair.first);
    }
    return codes;
}

std::vector<std::string> ProductDatabase::GetAllAwipsNames() const {
    std::vector<std::string> names;
    for (const auto& pair : awips_to_code_) {
        names.push_back(pair.first);
    }
    return names;
}

} // namespace leveliii
