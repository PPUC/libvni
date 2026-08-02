#include "vni_deflate.h"

#include <array>
#include <vector>

namespace vni {
namespace {

// The DEFLATE decoder below follows the compact inflate/Huffman approach in
// Mark Adler's puff.c reference implementation from zlib (zlib License),
// which supplements RFC 1951. See:
// https://github.com/madler/zlib/blob/master/contrib/puff/puff.c

class DeflateBitReader {
 public:
  DeflateBitReader(const uint8_t* data, size_t size)
      : data_(data), size_(size) {}

  bool read(int count, uint32_t* value) {
    if (count < 0 || count > 32 || !value) {
      return false;
    }
    while (bits_ < count) {
      if (position_ >= size_ || bits_ > 56) {
        return false;
      }
      buffer_ |= static_cast<uint64_t>(data_[position_++]) << bits_;
      bits_ += 8;
    }
    uint64_t mask = count == 32 ? 0xffffffffULL : ((1ULL << count) - 1);
    *value = static_cast<uint32_t>(buffer_ & mask);
    buffer_ >>= count;
    bits_ -= count;
    return true;
  }

  void align() {
    buffer_ = 0;
    bits_ = 0;
  }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t position_ = 0;
  uint64_t buffer_ = 0;
  int bits_ = 0;
};

class DeflateHuffmanTree {
 public:
  bool build(const std::vector<uint8_t>& lengths) {
    for (auto& codes : codes_) {
      codes.clear();
    }
    std::array<int, 16> counts{};
    for (uint8_t length : lengths) {
      if (length > 15) {
        return false;
      }
      if (length > 0) {
        counts[length]++;
      }
    }

    std::array<int, 16> next_code{};
    int code = 0;
    for (int length = 1; length <= 15; length++) {
      code = (code + counts[length - 1]) << 1;
      next_code[length] = code;
    }
    if (code + counts[15] > (1 << 15)) {
      return false;
    }

    for (size_t symbol = 0; symbol < lengths.size(); symbol++) {
      int length = lengths[symbol];
      if (length == 0) {
        continue;
      }
      int assigned_code = next_code[length]++;
      codes_[length].push_back(
          {static_cast<uint16_t>(assigned_code), static_cast<uint16_t>(symbol)});
    }
    return true;
  }

  bool decode(DeflateBitReader* reader, uint32_t* symbol) const {
    if (!reader || !symbol) {
      return false;
    }
    uint32_t code = 0;
    for (int length = 1; length <= 15; length++) {
      uint32_t direction = 0;
      if (!reader->read(1, &direction)) {
        return false;
      }
      code = (code << 1) | direction;
      for (const auto& entry : codes_[length]) {
        if (entry.first == code) {
          *symbol = entry.second;
          return true;
        }
      }
    }
    return false;
  }

 private:
  std::array<std::vector<std::pair<uint16_t, uint16_t>>, 16> codes_;
};

bool inflate_deflate(const uint8_t* data, size_t size,
                     std::vector<uint8_t>* output) {
  if (!data || !output) {
    return false;
  }
  output->clear();
  DeflateBitReader reader(data, size);
  constexpr size_t kMaxOutputSize = 256 * 1024 * 1024;
  constexpr std::array<int, 29> kLengthBase = {
      3,   4,   5,   6,   7,   8,   9,   10,  11,  13,  15,  17,  19,  23,
      27,  31,  35,  43,  51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  constexpr std::array<int, 29> kLengthExtra = {
      0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  constexpr std::array<int, 30> kDistanceBase = {
      1,     2,     3,     4,     5,     7,     9,     13,     17,     25,
      33,    49,    65,    97,    129,   193,   257,    385,    513,    769,
      1025,  1537,  2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  constexpr std::array<int, 30> kDistanceExtra = {
      0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  auto build_fixed_trees = [](DeflateHuffmanTree* literals,
                              DeflateHuffmanTree* distances) {
    std::vector<uint8_t> literal_lengths(288, 0);
    for (int i = 0; i <= 143; i++) literal_lengths[i] = 8;
    for (int i = 144; i <= 255; i++) literal_lengths[i] = 9;
    for (int i = 256; i <= 279; i++) literal_lengths[i] = 7;
    for (int i = 280; i <= 287; i++) literal_lengths[i] = 8;
    std::vector<uint8_t> distance_lengths(32, 5);
    return literals->build(literal_lengths) && distances->build(distance_lengths);
  };

  auto build_dynamic_trees = [&](DeflateHuffmanTree* literals,
                                 DeflateHuffmanTree* distances) {
    uint32_t value = 0;
    if (!reader.read(5, &value)) return false;
    int literal_count = static_cast<int>(value) + 257;
    if (!reader.read(5, &value)) return false;
    int distance_count = static_cast<int>(value) + 1;
    if (!reader.read(4, &value)) return false;
    int code_length_count = static_cast<int>(value) + 4;

    constexpr std::array<int, 19> kCodeLengthOrder = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    std::vector<uint8_t> code_length_lengths(19, 0);
    for (int i = 0; i < code_length_count; i++) {
      if (!reader.read(3, &value)) return false;
      code_length_lengths[kCodeLengthOrder[i]] = static_cast<uint8_t>(value);
    }
    DeflateHuffmanTree code_length_tree;
    if (!code_length_tree.build(code_length_lengths)) return false;

    std::vector<uint8_t> lengths(literal_count + distance_count, 0);
    for (size_t i = 0; i < lengths.size();) {
      uint32_t symbol = 0;
      if (!code_length_tree.decode(&reader, &symbol)) return false;
      if (symbol <= 15) {
        lengths[i++] = static_cast<uint8_t>(symbol);
      } else if (symbol == 16) {
        if (i == 0 || !reader.read(2, &value)) return false;
        int repeat = static_cast<int>(value) + 3;
        if (i + repeat > lengths.size()) return false;
        uint8_t previous = lengths[i - 1];
        while (repeat-- > 0) lengths[i++] = previous;
      } else if (symbol == 17) {
        if (!reader.read(3, &value)) return false;
        int repeat = static_cast<int>(value) + 3;
        if (i + repeat > lengths.size()) return false;
        while (repeat-- > 0) lengths[i++] = 0;
      } else if (symbol == 18) {
        if (!reader.read(7, &value)) return false;
        int repeat = static_cast<int>(value) + 11;
        if (i + repeat > lengths.size()) return false;
        while (repeat-- > 0) lengths[i++] = 0;
      } else {
        return false;
      }
    }
    std::vector<uint8_t> literal_lengths(lengths.begin(),
                                          lengths.begin() + literal_count);
    std::vector<uint8_t> distance_lengths(lengths.begin() + literal_count,
                                          lengths.end());
    return literals->build(literal_lengths) && distances->build(distance_lengths);
  };

  bool final_block = false;
  while (!final_block) {
    uint32_t value = 0;
    if (!reader.read(1, &value)) return false;
    final_block = value != 0;
    if (!reader.read(2, &value)) return false;
    int block_type = static_cast<int>(value);
    if (block_type == 0) {
      reader.align();
      uint32_t length = 0;
      uint32_t inverse = 0;
      if (!reader.read(16, &length) || !reader.read(16, &inverse) ||
          ((length ^ inverse) & 0xffff) != 0xffff) {
        return false;
      }
      for (uint32_t i = 0; i < length; i++) {
        if (!reader.read(8, &value) || output->size() >= kMaxOutputSize) {
          return false;
        }
        output->push_back(static_cast<uint8_t>(value));
      }
      continue;
    }
    if (block_type == 3) return false;

    DeflateHuffmanTree literal_tree;
    DeflateHuffmanTree distance_tree;
    if (block_type == 1) {
      if (!build_fixed_trees(&literal_tree, &distance_tree)) return false;
    } else if (!build_dynamic_trees(&literal_tree, &distance_tree)) {
      return false;
    }

    while (true) {
      uint32_t symbol = 0;
      if (!literal_tree.decode(&reader, &symbol)) return false;
      if (symbol < 256) {
        if (output->size() >= kMaxOutputSize) return false;
        output->push_back(static_cast<uint8_t>(symbol));
        continue;
      }
      if (symbol == 256) break;
      if (symbol < 257 || symbol > 285) return false;
      int length_index = static_cast<int>(symbol - 257);
      int length = kLengthBase[length_index];
      if (kLengthExtra[length_index] > 0) {
        if (!reader.read(kLengthExtra[length_index], &value)) return false;
        length += static_cast<int>(value);
      }
      if (!distance_tree.decode(&reader, &symbol) || symbol > 29) return false;
      int distance = kDistanceBase[symbol];
      if (kDistanceExtra[symbol] > 0) {
        if (!reader.read(kDistanceExtra[symbol], &value)) return false;
        distance += static_cast<int>(value);
      }
      if (distance > static_cast<int>(output->size()) ||
          output->size() > kMaxOutputSize - static_cast<size_t>(length)) {
        return false;
      }
      size_t start = output->size() - static_cast<size_t>(distance);
      for (int i = 0; i < length; i++) {
        output->push_back((*output)[start + static_cast<size_t>(i)]);
      }
    }
  }
  return true;
}

bool gzip_decompress_impl(const std::vector<uint8_t>& input,
                     std::vector<uint8_t>* output) {
  if (!output || input.size() < 18 || input[0] != 0x1f || input[1] != 0x8b ||
      input[2] != 8 || (input[3] & 0xe0) != 0) {
    return false;
  }
  size_t header_size = 10;
  uint8_t flags = input[3];
  if (flags & 0x04) {
    if (header_size + 2 > input.size()) return false;
    size_t extra_size = input[header_size] | (input[header_size + 1] << 8);
    header_size += 2;
    if (extra_size > input.size() - header_size) return false;
    header_size += extra_size;
  }
  for (uint8_t flag : {static_cast<uint8_t>(0x08),
                       static_cast<uint8_t>(0x10)}) {
    if (flags & flag) {
      while (header_size < input.size() && input[header_size++] != 0) {
      }
      if (header_size > input.size()) return false;
    }
  }
  if (flags & 0x02) {
    if (header_size + 2 > input.size()) return false;
    header_size += 2;
  }
  if (header_size + 8 > input.size()) return false;
  return inflate_deflate(input.data() + header_size,
                         input.size() - header_size - 8, output);
}


}  // namespace

bool gzip_decompress(const std::vector<uint8_t>& input,
                      std::vector<uint8_t>* output) {
  return gzip_decompress_impl(input, output);
}

}  // namespace vni
