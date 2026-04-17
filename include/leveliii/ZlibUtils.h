#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

namespace leveliii {
namespace ZlibUtils {

std::vector<uint8_t> gzip_compress(const uint8_t* data, size_t data_size);
std::vector<uint8_t> gzip_decompress(const uint8_t* data, size_t data_size);

} // namespace ZlibUtils
} // namespace leveliii
