#include "LevelIII_Types.h"
#include "leveliii/ProductDatabase.h"

namespace leveliii {

const char* GetProductName(int16_t code) {
    return ProductDatabase::Instance().GetProductName(code);
}

const char* GetProductCategory(int16_t code) {
    return ProductDatabase::Instance().GetProductCategory(code);
}

const char* GetProductStorageCategory(int16_t code) {
    return ProductDatabase::Instance().GetProductStorageCategory(code);
}

} // namespace leveliii
