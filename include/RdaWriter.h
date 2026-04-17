#pragma once

#include "LevelIII_Types.h"
#include <string>
#include <vector>

namespace leveliii {

class RdaWriter {
public:
    static bool Write(const std::string& filename, const RadarFrame& frame);

private:
    static std::string CreateMetadataJson(const RadarFrame& frame, uint32_t valid_count);
};

} // namespace leveliii
