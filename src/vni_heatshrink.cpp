#include "vni_heatshrink.h"

#include <stddef.h>

namespace vni {

namespace {

class BitReader {
 public:
  BitReader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

  bool read_bits(int count, uint32_t* out) {
    uint32_t value = 0;
    for (int i = 0; i < count; i++) {
      if (!fill()) {
        return false;
      }
      value |= (uint32_t)(bitbuf_ & 1u) << i;
      bitbuf_ >>= 1;
      bits_in_buf_--;
    }
    *out = value;
    return true;
  }

 private:
  bool fill() {
    if (bits_in_buf_ > 0) {
      return true;
    }
    if (pos_ >= len_) {
      return false;
    }
    bitbuf_ = data_[pos_++];
    bits_in_buf_ = 8;
    return true;
  }

  const uint8_t* data_;
  size_t len_;
  size_t pos_ = 0;
  uint32_t bitbuf_ = 0;
  int bits_in_buf_ = 0;
};

}  // namespace

bool heatshrink_decompress(const uint8_t* data, size_t len, int window_sz,
                           int lookahead_sz, std::vector<uint8_t>* out) {
  if (!out) {
    return false;
  }
  out->clear();

  BitReader reader(data, len);
  while (true) {
    uint32_t flag = 0;
    if (!reader.read_bits(1, &flag)) {
      break;
    }
    if (flag == 1) {
      uint32_t literal = 0;
      if (!reader.read_bits(8, &literal)) {
        return false;
      }
      out->push_back(static_cast<uint8_t>(literal));
      continue;
    }

    uint32_t offset = 0;
    uint32_t count = 0;
    if (!reader.read_bits(window_sz, &offset)) {
      return false;
    }
    if (!reader.read_bits(lookahead_sz, &count)) {
      return false;
    }

    offset += 1;
    count += 1;
    if (offset > out->size()) {
      return false;
    }
    size_t start = out->size() - offset;
    for (uint32_t i = 0; i < count; i++) {
      out->push_back((*out)[start + i]);
    }
  }

  return true;
}

}  // namespace vni
