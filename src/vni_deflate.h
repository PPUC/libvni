#ifndef VNI_DEFLATE_H
#define VNI_DEFLATE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vni {

bool gzip_decompress(const std::vector<uint8_t>& input,
                    std::vector<uint8_t>* output);

}  // namespace vni

#endif