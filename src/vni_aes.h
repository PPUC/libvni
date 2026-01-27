#pragma once

#include <stdint.h>

#include <vector>

namespace vni {

std::vector<uint8_t> aes128_cbc_decrypt(const uint8_t* data, size_t len,
                                        const uint8_t key[16],
                                        const uint8_t iv[16]);

}  // namespace vni
