#pragma once

#include <stdint.h>

#include <vector>

namespace vni {

bool heatshrink_decompress(const uint8_t* data, size_t len, int window_sz,
                           int lookahead_sz, std::vector<uint8_t>* out);

}  // namespace vni
