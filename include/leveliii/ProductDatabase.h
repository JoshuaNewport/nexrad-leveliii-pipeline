#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace leveliii {

struct ProductInfo {
    int16_t code;
    const char* name;
    const char* description;
    const char* category;
    const char* storage_category;
    const char* awips_names[9];
    bool is_compressed;
    uint16_t supported_packet_types[8];
};

class ProductDatabase {
public:
    static ProductDatabase& Instance();

    const ProductInfo* GetProductInfo(int16_t code) const;
    const ProductInfo* FindByAwipsName(const std::string& awips_name) const;
    
    const char* GetProductName(int16_t code) const;
    const char* GetProductDescription(int16_t code) const;
    const char* GetProductCategory(int16_t code) const;
    const char* GetProductStorageCategory(int16_t code) const;
    
    bool IsCompressed(int16_t code) const;
    bool SupportsPacketType(int16_t product_code, uint16_t packet_code) const;
    
    std::vector<int16_t> GetAllProductCodes() const;
    std::vector<std::string> GetAllAwipsNames() const;

private:
    ProductDatabase();
    ~ProductDatabase() = default;

    void InitializeProducts();
    void BuildAwipsLookup();

    std::unordered_map<int16_t, ProductInfo> products_;
    std::unordered_map<std::string, int16_t> awips_to_code_;
};

} // namespace leveliii
