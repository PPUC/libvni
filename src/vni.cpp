#include "vni.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "FrameUtil.h"
#include "vni_heatshrink.h"
#include "vni_internal.h"

namespace vni {

namespace {

constexpr uint32_t kDefaultWidth = 128;
constexpr uint32_t kDefaultHeight = 32;

uint16_t read_u16_be(std::istream& in) {
  uint8_t buf[2];
  in.read(reinterpret_cast<char*>(buf), 2);
  return static_cast<uint16_t>((buf[0] << 8) | buf[1]);
}

uint32_t read_u32_be(std::istream& in) {
  uint8_t buf[4];
  in.read(reinterpret_cast<char*>(buf), 4);
  return (static_cast<uint32_t>(buf[0]) << 24) |
         (static_cast<uint32_t>(buf[1]) << 16) |
         (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
}

std::vector<uint8_t> read_bytes(std::istream& in, size_t len) {
  std::vector<uint8_t> out(len);
  in.read(reinterpret_cast<char*>(out.data()), len);
  out.resize(static_cast<size_t>(in.gcount()));
  return out;
}

std::vector<uint8_t> read_bytes_required(std::istream& in, size_t len) {
  std::vector<uint8_t> out(len);
  in.read(reinterpret_cast<char*>(out.data()), len);
  if (static_cast<size_t>(in.gcount()) != len) {
    out.clear();
  }
  return out;
}

uint8_t reverse_bits(uint8_t a) { return FrameUtil::Helper::ReverseByte(a); }

Palette* find_palette(PalFile* pal, uint16_t palette_index) {
  if (!pal) {
    return nullptr;
  }
  for (auto& palette : pal->palettes) {
    if (palette.index == palette_index) {
      return &palette;
    }
  }
  return nullptr;
}

std::vector<uint8_t> expand_palette(const Palette& palette, size_t colors) {
  std::vector<uint8_t> out;
  out.resize(colors * 3, 0);
  size_t available = palette.colors.size() / 3;
  if (available == 0) {
    return out;
  }
  for (size_t i = 0; i < colors; i++) {
    size_t src = std::min(i, available - 1);
    out[i * 3 + 0] = palette.colors[src * 3 + 0];
    out[i * 3 + 1] = palette.colors[src * 3 + 1];
    out[i * 3 + 2] = palette.colors[src * 3 + 2];
  }
  return out;
}

std::vector<std::vector<uint8_t>> split_planes(const uint8_t* frame,
                                               uint32_t width, uint32_t height,
                                               uint8_t bitlen) {
  size_t plane_size = static_cast<size_t>(width) * height / 8;
  std::vector<uint8_t> packed(bitlen * plane_size, 0);
  FrameUtil::Helper::Split(packed.data(), static_cast<uint16_t>(width),
                           static_cast<uint16_t>(height), bitlen,
                           const_cast<uint8_t*>(frame));

  std::vector<std::vector<uint8_t>> planes(bitlen,
                                           std::vector<uint8_t>(plane_size, 0));
  for (uint8_t i = 0; i < bitlen; i++) {
    std::copy(packed.begin() + i * plane_size,
              packed.begin() + (i + 1) * plane_size, planes[i].begin());
  }
  return planes;
}

std::vector<uint8_t> join_planes(
    const std::vector<std::vector<uint8_t>>& planes, const Dimensions& dim) {
  size_t surface = static_cast<size_t>(dim.width) * dim.height;
  std::vector<uint8_t> data(surface, 0);
  if (planes.empty()) {
    return data;
  }
  size_t plane_size = planes[0].size();
  std::vector<uint8_t> packed(planes.size() * plane_size, 0);
  for (size_t i = 0; i < planes.size(); i++) {
    std::copy(planes[i].begin(), planes[i].end(),
              packed.begin() + i * plane_size);
  }
  FrameUtil::Helper::Join(data.data(), static_cast<uint16_t>(dim.width),
                          static_cast<uint16_t>(dim.height),
                          static_cast<uint8_t>(planes.size()), packed.data());

  return data;
}

std::vector<uint8_t> scale_double_indexed(const std::vector<uint8_t>& data,
                                          const Dimensions& dim) {
  Dimensions out_dim(dim.width * 2, dim.height * 2);
  std::vector<uint8_t> out(out_dim.surface(), 0);
  FrameUtil::Helper::ScaleDoubleIndexed(out.data(), data.data(),
                                        static_cast<uint16_t>(dim.width),
                                        static_cast<uint16_t>(dim.height));
  return out;
}

std::vector<uint8_t> scale2x_indexed(const std::vector<uint8_t>& data,
                                     const Dimensions& dim) {
  Dimensions out_dim(dim.width * 2, dim.height * 2);
  std::vector<uint8_t> out(out_dim.surface(), 0);
  FrameUtil::Helper::Scale2XIndexed(out.data(), data.data(),
                                    static_cast<uint16_t>(dim.width),
                                    static_cast<uint16_t>(dim.height));
  return out;
}

void clear_plane(std::vector<uint8_t>& plane) {
  FrameUtil::Helper::ClearPlane(plane.data(), plane.size());
}

void or_plane(const std::vector<uint8_t>& src, std::vector<uint8_t>& dest) {
  size_t count = std::min(src.size(), dest.size());
  FrameUtil::Helper::OrPlane(src.data(), dest.data(), count);
}

std::vector<uint8_t> combine_plane_with_mask(
    const std::vector<uint8_t>& base, const std::vector<uint8_t>& overlay,
    const std::vector<uint8_t>& mask) {
  size_t count = std::min({base.size(), overlay.size(), mask.size()});
  std::vector<uint8_t> out(count, 0);
  FrameUtil::Helper::CombinePlaneWithMask(base.data(), overlay.data(),
                                          mask.data(), out.data(), count);
  return out;
}

uint32_t checksum_plane(const std::vector<uint8_t>& plane, bool reverse) {
  return FrameUtil::Helper::Checksum(plane.data(), plane.size(), reverse);
}

uint32_t checksum_plane_with_mask(const std::vector<uint8_t>& plane,
                                  const std::vector<uint8_t>& mask,
                                  bool reverse) {
  size_t count = std::min(plane.size(), mask.size());
  return FrameUtil::Helper::ChecksumWithMask(plane.data(), mask.data(), count,
                                             reverse);
}

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

uint32_t Context::tick() const { return static_cast<uint32_t>(now_ms()); }

static bool read_pal_file(std::istream& in, PalFile* pal) {
  pal->version = static_cast<uint8_t>(in.get());
  if (!in.good()) {
    return false;
  }
  uint16_t num_palettes = read_u16_be(in);
  pal->palettes.clear();
  pal->palettes.reserve(num_palettes);
  pal->default_palette_index = -1;

  for (uint16_t i = 0; i < num_palettes; i++) {
    Palette palette;
    palette.index = read_u16_be(in);
    uint16_t num_colors = read_u16_be(in);
    palette.type = static_cast<uint8_t>(in.get());
    palette.colors.resize(static_cast<size_t>(num_colors) * 3);
    for (size_t j = 0; j < palette.colors.size(); j++) {
      palette.colors[j] = static_cast<uint8_t>(in.get());
    }
    if (pal->default_palette_index < 0 && palette.is_default()) {
      pal->default_palette_index = static_cast<int>(pal->palettes.size());
    }
    pal->palettes.push_back(std::move(palette));
  }
  if (pal->default_palette_index < 0 && !pal->palettes.empty()) {
    pal->default_palette_index = 0;
  }

  if (in.peek() == std::char_traits<char>::eof()) {
    return true;
  }

  uint16_t num_mappings = read_u16_be(in);
  pal->mappings.clear();
  for (uint16_t i = 0; i < num_mappings; i++) {
    Mapping mapping;
    mapping.checksum = read_u32_be(in);
    mapping.mode = static_cast<SwitchMode>(in.get());
    mapping.palette_index = read_u16_be(in);
    if (mapping.mode == SwitchMode::Palette) {
      mapping.duration = read_u32_be(in);
    } else {
      mapping.offset = read_u32_be(in);
    }
    pal->mappings.emplace(mapping.checksum, mapping);
  }

  if (in.peek() == std::char_traits<char>::eof()) {
    return true;
  }

  uint8_t num_masks = static_cast<uint8_t>(in.get());
  if (num_masks > 0) {
    std::streampos pos = in.tellg();
    in.seekg(0, std::ios::end);
    std::streampos end = in.tellg();
    in.seekg(pos);
    size_t remaining = static_cast<size_t>(end - pos);
    size_t mask_bytes = remaining / num_masks;
    if (mask_bytes != 256 && mask_bytes != 512 && mask_bytes != 1536) {
      return true;
    }
    pal->masks.clear();
    pal->masks.reserve(num_masks);
    for (uint8_t i = 0; i < num_masks; i++) {
      auto mask = read_bytes_required(in, mask_bytes);
      if (mask.empty()) {
        return false;
      }
      pal->masks.push_back(std::move(mask));
    }
  }
  return true;
}

static bool read_vni_frame_seq(std::istream& in, int file_version,
                               FrameSeq* seq) {
  uint16_t name_len = read_u16_be(in);
  if (name_len > 0) {
    auto name_bytes = read_bytes(in, name_len);
    seq->name.assign(reinterpret_cast<char*>(name_bytes.data()),
                     name_bytes.size());
  } else {
    seq->name = "<undefined>";
  }

  read_u16_be(in);  // cycles
  read_u16_be(in);  // hold cycles
  read_u16_be(in);  // clock from
  in.get();         // clock small
  in.get();         // clock in front
  read_u16_be(in);  // clock offset x
  read_u16_be(in);  // clock offset y
  read_u16_be(in);  // refresh delay
  in.get();         // type
  in.get();         // fsk

  int num_frames = static_cast<int16_t>(read_u16_be(in));
  if (num_frames < 0) {
    num_frames += 65536;
  }

  if (file_version >= 2) {
    read_u16_be(in);
    uint16_t num_colors = read_u16_be(in);
    if (num_colors > 0) {
      read_bytes(in, static_cast<size_t>(num_colors) * 3);
    }
  }
  if (file_version >= 3) {
    in.get();  // edit mode
  }
  if (file_version >= 4) {
    seq->size.width = read_u16_be(in);
    seq->size.height = read_u16_be(in);
  } else {
    seq->size = Dimensions(kDefaultWidth, kDefaultHeight);
  }
  if (file_version >= 5) {
    uint16_t num_masks = read_u16_be(in);
    seq->masks.clear();
    seq->masks.reserve(num_masks);
    for (uint16_t i = 0; i < num_masks; i++) {
      in.get();  // locked
      uint16_t size = read_u16_be(in);
      auto mask = read_bytes_required(in, size);
      if (mask.empty()) {
        return false;
      }
      for (auto& b : mask) {
        b = reverse_bits(b);
      }
      seq->masks.push_back(std::move(mask));
    }
  }
  if (file_version >= 6) {
    in.get();  // compiled animation
    uint16_t size = read_u16_be(in);
    if (size > 0) {
      read_bytes(in, size);
    }
    read_u32_be(in);  // start frame
  }

  seq->frames.clear();
  seq->frames.reserve(num_frames);
  seq->animation_duration = 0;

  for (int i = 0; i < num_frames; i++) {
    AnimationFrame frame;
    frame.time = seq->animation_duration;
    int plane_size = static_cast<int16_t>(read_u16_be(in));
    frame.delay = static_cast<uint32_t>(read_u16_be(in));
    if (file_version >= 4) {
      frame.hash = read_u32_be(in);
    }
    frame.bit_length = static_cast<uint8_t>(in.get());

    bool compressed = false;
    if (file_version >= 3) {
      compressed = in.get() != 0;
    }

    auto read_planes = [&](std::istream& reader) -> bool {
      frame.planes.clear();
      frame.planes.reserve(frame.bit_length);
      for (uint8_t p = 0; p < frame.bit_length; p++) {
        int marker = reader.get();
        if (marker == std::char_traits<char>::eof()) {
          return false;
        }
        if (marker == 0x6d) {
          frame.mask = read_bytes_required(reader, plane_size);
          if (frame.mask.empty()) {
            return false;
          }
          for (auto& b : frame.mask) {
            b = reverse_bits(b);
          }
        } else {
          AnimationPlane plane;
          plane.marker = static_cast<uint8_t>(marker);
          plane.plane = read_bytes_required(reader, plane_size);
          if (plane.plane.empty()) {
            return false;
          }
          for (auto& b : plane.plane) {
            b = reverse_bits(b);
          }
          frame.planes.push_back(std::move(plane));
        }
      }
      return true;
    };

    if (!compressed) {
      if (!read_planes(in)) {
        return false;
      }
    } else {
      uint32_t compressed_size = read_u32_be(in);
      auto compressed_bytes = read_bytes_required(in, compressed_size);
      std::vector<uint8_t> decompressed;
      if (!heatshrink_decompress(compressed_bytes.data(),
                                 compressed_bytes.size(), 10, 5,
                                 &decompressed)) {
        return false;
      }
      std::stringstream reader(std::string(
          reinterpret_cast<char*>(decompressed.data()), decompressed.size()));
      if (!read_planes(reader)) {
        return false;
      }
    }
    seq->frames.push_back(std::move(frame));
    seq->animation_duration += seq->frames.back().delay;
  }

  return true;
}

static bool read_vni_file(std::istream& in, VniFile* vni) {
  auto header = read_bytes(in, 4);
  if (header.size() != 4 ||
      std::string(reinterpret_cast<char*>(header.data()), 4) != "VPIN") {
    return false;
  }
  vni->version = static_cast<uint16_t>(read_u16_be(in));
  uint16_t num_animations = read_u16_be(in);
  if (vni->version >= 2) {
    for (uint16_t i = 0; i < num_animations; i++) {
      read_u32_be(in);
    }
  }
  vni->animations.clear();
  vni->animations.reserve(num_animations);
  uint32_t max_w = 0;
  uint32_t max_h = 0;
  for (uint16_t i = 0; i < num_animations; i++) {
    FrameSeq seq;
    seq.offset = static_cast<uint32_t>(in.tellg());
    if (!read_vni_frame_seq(in, vni->version, &seq)) {
      return false;
    }
    max_w = std::max(max_w, seq.size.width);
    max_h = std::max(max_h, seq.size.height);
    vni->animations.push_back(std::move(seq));
  }
  vni->dimensions = Dimensions(max_w, max_h);
  return true;
}

static FrameSeq* find_animation(VniFile* vni, uint32_t offset) {
  if (!vni) {
    return nullptr;
  }
  for (auto& seq : vni->animations) {
    if (seq.offset == offset) {
      return &seq;
    }
  }
  return nullptr;
}

static Mapping* find_mapping(PalFile* pal, const std::vector<uint8_t>& plane,
                             bool reverse, uint32_t* no_mask_crc) {
  if (!pal) {
    return nullptr;
  }
  uint32_t checksum = checksum_plane(plane, reverse);
  if (no_mask_crc) {
    *no_mask_crc = checksum;
  }
  auto it = pal->mappings.find(checksum);
  if (it != pal->mappings.end()) {
    return &it->second;
  }
  if (pal->masks.empty()) {
    return nullptr;
  }
  for (const auto& mask : pal->masks) {
    checksum = checksum_plane_with_mask(plane, mask, reverse);
    it = pal->mappings.find(checksum);
    if (it != pal->mappings.end()) {
      return &it->second;
    }
  }
  return nullptr;
}

static std::vector<std::vector<uint8_t>> render_color_mask(
    const FrameSeq& seq, const std::vector<std::vector<uint8_t>>& vpm_frame,
    uint32_t frame_index) {
  std::vector<std::vector<uint8_t>> out;
  if (seq.frames.empty()) {
    return out;
  }
  const auto& frame = seq.frames[frame_index];
  size_t frame_count = frame.planes.size();
  out.resize(frame_count);
  if (frame_count < 4) {
    return vpm_frame;
  }
  if (vpm_frame.size() == frame_count) {
    for (size_t i = 0; i + 2 < vpm_frame.size(); i++) {
      out[i] = vpm_frame[i];
    }
    for (size_t i = vpm_frame.size() - 2; i < frame_count; i++) {
      out[i] = frame.planes[i].plane;
    }
  } else {
    for (size_t i = 0; i < vpm_frame.size(); i++) {
      out[i] = vpm_frame[i];
    }
    for (size_t i = vpm_frame.size(); i < frame_count; i++) {
      out[i] = frame.planes[i].plane;
    }
  }
  return out;
}

static std::vector<std::vector<uint8_t>> render_lcm(
    FrameSeq& seq, const Dimensions& dim,
    std::vector<std::vector<uint8_t>> planes, ScalerMode scaler_mode) {
  size_t num_planes = seq.lcm_buffer_planes.size();
  std::vector<std::vector<uint8_t>> outplanes(num_planes);

  if (seq.switch_mode == SwitchMode::LayeredColorMask) {
    for (size_t i = 0; i < planes.size() && i < num_planes; i++) {
      outplanes[i] = planes[i];
    }
    for (size_t i = planes.size(); i < num_planes; i++) {
      outplanes[i] = seq.lcm_buffer_planes[i];
    }
    return outplanes;
  }

  if (seq.switch_mode == SwitchMode::MaskedReplace) {
    if (!planes.empty() &&
        seq.lcm_buffer_planes[0].size() == planes[0].size() * 4) {
      auto indexed = join_planes(planes, dim);
      Dimensions scaled(dim.width * 2, dim.height * 2);
      auto scaled_data = (scaler_mode == ScalerMode::Scale2x)
                             ? scale2x_indexed(indexed, dim)
                             : scale_double_indexed(indexed, dim);
      planes = split_planes(scaled_data.data(), scaled.width, scaled.height,
                            static_cast<uint8_t>(planes.size()));
    }
    for (size_t i = 0; i < num_planes; i++) {
      if (i < planes.size()) {
        outplanes[i] = combine_plane_with_mask(seq.lcm_buffer_planes[i],
                                               planes[i], seq.replace_mask);
      } else {
        outplanes[i] = seq.lcm_buffer_planes[i];
      }
    }
    return outplanes;
  }

  return planes;
}

static void start_lcm(FrameSeq& seq) {
  seq.lcm_buffer_planes.clear();
  if (seq.frames.empty()) {
    return;
  }
  size_t plane_count = seq.frames[0].planes.size();
  for (size_t i = 0; i < plane_count; i++) {
    seq.lcm_buffer_planes.emplace_back(
        FrameUtil::Helper::NewPlane(static_cast<uint16_t>(seq.size.width),
                                    static_cast<uint16_t>(seq.size.height)));
  }
  for (auto& plane : seq.lcm_buffer_planes) {
    clear_plane(plane);
  }
  if (seq.switch_mode == SwitchMode::MaskedReplace) {
    seq.replace_mask =
        FrameUtil::Helper::NewPlane(static_cast<uint16_t>(seq.size.width),
                                    static_cast<uint16_t>(seq.size.height));
  }
}

static void start_replace(FrameSeq& seq) {
  seq.last_tick = now_ms();
  seq.timer = 0;
}

static void start_enhance(FrameSeq& seq) {
  seq.last_tick = now_ms();
  seq.timer = 0;
}

static void initialize_frame(FrameSeq& seq) {
  if (seq.frame_index < seq.frames.size()) {
    seq.timer += static_cast<int64_t>(seq.frames[seq.frame_index].delay);
  }
}

static void output_frame(Context* ctx, FrameSeq& seq, const Dimensions& dim,
                         const std::vector<std::vector<uint8_t>>& planes) {
  std::vector<std::vector<uint8_t>> outplanes;
  switch (seq.switch_mode) {
    case SwitchMode::ColorMask:
    case SwitchMode::Follow:
      outplanes = render_color_mask(seq, planes, seq.frame_index);
      break;
    case SwitchMode::Replace:
    case SwitchMode::FollowReplace:
      outplanes.clear();
      if (seq.frame_index < seq.frames.size()) {
        for (const auto& plane : seq.frames[seq.frame_index].planes) {
          outplanes.push_back(plane.plane);
        }
      }
      break;
    case SwitchMode::LayeredColorMask:
    case SwitchMode::MaskedReplace:
      outplanes = render_lcm(seq, dim, planes, ctx->scaler_mode);
      break;
    default:
      outplanes = planes;
      break;
  }

  Dimensions out_dim = dim;
  if (!outplanes.empty() && outplanes[0].size() == dim.surface() / 2) {
    out_dim = Dimensions(dim.width * 2, dim.height * 2);
  }

  ctx->output.data = join_planes(outplanes, out_dim);
  ctx->output.dimensions = out_dim;
  ctx->output.bitlen = static_cast<uint8_t>(outplanes.size());
  ctx->output.has_frame = true;
}

static void render_animation(Context* ctx, FrameSeq& seq, const Dimensions& dim,
                             const std::vector<std::vector<uint8_t>>& planes) {
  if (seq.switch_mode == SwitchMode::ColorMask ||
      seq.switch_mode == SwitchMode::Replace) {
    int64_t delay = now_ms() - seq.last_tick;
    seq.last_tick = now_ms();
    seq.timer -= delay;
    if (seq.timer > 0) {
      if (seq.frame_index > 0) {
        seq.frame_index--;
      }
      output_frame(ctx, seq, dim, planes);
      seq.frame_index++;
      return;
    }
  }

  if (seq.frame_index < seq.frames.size()) {
    if (seq.switch_mode == SwitchMode::LayeredColorMask ||
        seq.switch_mode == SwitchMode::MaskedReplace ||
        seq.switch_mode == SwitchMode::Follow ||
        seq.switch_mode == SwitchMode::FollowReplace) {
      output_frame(ctx, seq, dim, planes);
      return;
    }

    initialize_frame(seq);
    output_frame(ctx, seq, dim, planes);
    seq.frame_index++;
    return;
  }

  seq.switch_mode = SwitchMode::Palette;
  output_frame(ctx, seq, dim, planes);
  seq.is_running = false;
  seq.frame_index = 0;
}

static void detect_follow(FrameSeq& seq, const std::vector<uint8_t>& plane,
                          uint32_t no_mask_crc,
                          const std::vector<std::vector<uint8_t>>& masks,
                          bool reverse) {
  uint32_t frame_index = 0;
  for (const auto& frame : seq.frames) {
    if (no_mask_crc == frame.hash) {
      seq.frame_index = frame_index;
      return;
    }
    if (!masks.empty()) {
      for (const auto& mask : masks) {
        uint32_t mask_crc = checksum_plane_with_mask(plane, mask, reverse);
        if (mask_crc == frame.hash) {
          seq.frame_index = frame_index;
          return;
        }
      }
    }
    frame_index++;
  }
}

static bool detect_lcm(FrameSeq& seq, const std::vector<uint8_t>& plane,
                       uint32_t no_mask_crc, bool reverse, bool clear) {
  uint32_t checksum = no_mask_crc;
  if (seq.masks.empty()) {
    return clear;
  }
  for (int k = -1; k < static_cast<int>(seq.masks.size()); k++) {
    if (k >= 0) {
      checksum = checksum_plane_with_mask(plane, seq.masks[k], reverse);
    }
    for (const auto& frame : seq.frames) {
      if (frame.hash == checksum) {
        if (clear) {
          for (auto& plane_buf : seq.lcm_buffer_planes) {
            clear_plane(plane_buf);
          }
          clear = false;
          if (seq.switch_mode == SwitchMode::MaskedReplace) {
            clear_plane(seq.replace_mask);
          }
        }
        for (size_t i = 0; i < frame.planes.size(); i++) {
          or_plane(frame.planes[i].plane, seq.lcm_buffer_planes[i]);
          if (seq.switch_mode == SwitchMode::MaskedReplace &&
              !frame.mask.empty()) {
            or_plane(frame.mask, seq.replace_mask);
          }
        }
      }
    }
  }
  return clear;
}

static void start_animation(Context* ctx, Mapping& mapping,
                            const Dimensions& dim,
                            const std::vector<std::vector<uint8_t>>& planes) {
  if (!ctx->pal) {
    return;
  }
  if (mapping.mode == SwitchMode::Event) {
    return;
  }
  if (ctx->active_seq &&
      (ctx->active_seq->switch_mode == SwitchMode::LayeredColorMask ||
       ctx->active_seq->switch_mode == SwitchMode::MaskedReplace) &&
      mapping.mode == ctx->active_seq->switch_mode &&
      mapping.offset == ctx->active_seq->offset) {
    return;
  }

  if (ctx->active_seq) {
    ctx->active_seq->is_running = false;
    ctx->active_seq = nullptr;
  }

  Palette* palette = find_palette(ctx->pal.get(), mapping.palette_index);
  if (!palette) {
    return;
  }
  ctx->palette = palette;
  ctx->palette_reset_at = -1;

  if (!mapping.is_animation() && mapping.duration > 0) {
    ctx->palette_reset_at = now_ms() + mapping.duration;
  }

  if (!mapping.is_animation()) {
    return;
  }

  if (!ctx->vni) {
    return;
  }
  ctx->active_seq = find_animation(ctx->vni.get(), mapping.offset);
  if (!ctx->active_seq) {
    return;
  }
  ctx->active_seq->switch_mode = mapping.mode;
  ctx->active_seq->frame_index = 0;
  ctx->active_seq->is_running = true;

  switch (mapping.mode) {
    case SwitchMode::ColorMask:
    case SwitchMode::Follow:
      start_enhance(*ctx->active_seq);
      break;
    case SwitchMode::Replace:
    case SwitchMode::FollowReplace:
      start_replace(*ctx->active_seq);
      break;
    case SwitchMode::LayeredColorMask:
    case SwitchMode::MaskedReplace:
      start_lcm(*ctx->active_seq);
      break;
    default:
      break;
  }

  render_animation(ctx, *ctx->active_seq, dim, planes);
}

static void trigger_animation(Context* ctx, const Dimensions& dim,
                              const std::vector<std::vector<uint8_t>>& planes,
                              bool reverse) {
  if (!ctx->pal || ctx->pal->mappings.empty()) {
    return;
  }
  uint32_t nomask_crc = 0;
  bool clear = true;
  for (const auto& plane : planes) {
    auto mapping = find_mapping(ctx->pal.get(), plane, reverse, &nomask_crc);
    if (mapping) {
      start_animation(ctx, *mapping, dim, planes);
      if (ctx->active_seq &&
          ctx->active_seq->switch_mode != SwitchMode::LayeredColorMask &&
          ctx->active_seq->switch_mode != SwitchMode::MaskedReplace) {
        return;
      }
    }
    if (ctx->active_seq) {
      if (ctx->active_seq->switch_mode == SwitchMode::LayeredColorMask ||
          ctx->active_seq->switch_mode == SwitchMode::MaskedReplace) {
        clear = detect_lcm(*ctx->active_seq, plane, nomask_crc, reverse, clear);
      } else if (ctx->active_seq->switch_mode == SwitchMode::Follow ||
                 ctx->active_seq->switch_mode == SwitchMode::FollowReplace) {
        detect_follow(*ctx->active_seq, plane, nomask_crc, ctx->pal->masks,
                      reverse);
      }
    }
  }
}

static void render(Context* ctx, const Dimensions& dim,
                   std::vector<std::vector<uint8_t>> planes) {
  if (!ctx->pal || !ctx->palette) {
    return;
  }
  Dimensions out_dim = dim;
  if (ctx->vni && (dim.width * 2 == ctx->vni->dimensions.width &&
                   dim.height * 2 == ctx->vni->dimensions.height)) {
    auto indexed = join_planes(planes, dim);
    if (ctx->scaler_mode == ScalerMode::Scale2x ||
        ctx->scaler_mode == ScalerMode::ScaleDouble) {
      indexed = (ctx->scaler_mode == ScalerMode::Scale2x)
                    ? scale2x_indexed(indexed, dim)
                    : scale_double_indexed(indexed, dim);
      out_dim = Dimensions(dim.width * 2, dim.height * 2);
      planes = split_planes(indexed.data(), out_dim.width, out_dim.height,
                            static_cast<uint8_t>(planes.size()));
    }
  }

  ctx->output.data = join_planes(planes, out_dim);
  ctx->output.dimensions = out_dim;
  ctx->output.bitlen = static_cast<uint8_t>(planes.size());
  ctx->output.has_frame = true;
}

static void maybe_reset_palette(Context* ctx) {
  if (ctx->palette_reset_at < 0) {
    return;
  }
  if (now_ms() >= ctx->palette_reset_at) {
    if (ctx->default_palette) {
      ctx->palette = ctx->default_palette;
    }
    ctx->palette_reset_at = -1;
  }
}

}  // namespace vni

using namespace vni;

Vni_Context* Vni_LoadFromPaths(const char* pal_path, const char* vni_path,
                               const char* pac_path, const char* vni_key) {
  auto ctx = std::make_unique<Context>();

  if (pac_path && pac_path[0] != '\0') {
    std::fprintf(stderr,
                 "VNI: encrypted PAC files are not supported; ignoring "
                 "pac_path.\n");
  }
  (void)vni_key;

  if (pal_path && pal_path[0] != '\0') {
    std::ifstream pal_file(pal_path, std::ios::binary);
    if (pal_file.is_open()) {
      auto pal = std::make_unique<PalFile>();
      if (!read_pal_file(pal_file, pal.get())) {
        return nullptr;
      }
      ctx->pal = std::move(pal);
    }
  }
  if (vni_path && vni_path[0] != '\0') {
    std::ifstream vni_file(vni_path, std::ios::binary);
    if (vni_file.is_open()) {
      auto vni_obj = std::make_unique<VniFile>();
      if (!read_vni_file(vni_file, vni_obj.get())) {
        return nullptr;
      }
      ctx->vni = std::move(vni_obj);
    }
  }

  if (!ctx->pal) {
    return nullptr;
  }

  if (ctx->pal->default_palette_index >= 0 &&
      ctx->pal->default_palette_index <
          static_cast<int>(ctx->pal->palettes.size())) {
    ctx->default_palette = &ctx->pal->palettes[ctx->pal->default_palette_index];
    ctx->palette = ctx->default_palette;
  }

  return reinterpret_cast<Vni_Context*>(ctx.release());
}

void Vni_Dispose(Vni_Context* ctx) {
  if (!ctx) {
    return;
  }
  auto* context = reinterpret_cast<Context*>(ctx);
  delete context;
}

const Vni_Frame_Struc* Vni_GetFrame(const Vni_Context* ctx) {
  if (!ctx) {
    return nullptr;
  }
  auto* context = reinterpret_cast<const Context*>(ctx);
  static Vni_Frame_Struc frame;
  frame.width = context->output.dimensions.width;
  frame.height = context->output.dimensions.height;
  frame.bitlen = context->output.bitlen;
  frame.has_frame = context->output.has_frame ? 1 : 0;
  frame.frame = context->output.data.data();
  frame.palette = context->output.palette.data();
  return &frame;
}

void Vni_SetScalerMode(Vni_Context* ctx, uint32_t mode) {
  if (!ctx) {
    return;
  }
  auto* context = reinterpret_cast<Context*>(ctx);
  context->scaler_mode = static_cast<ScalerMode>(mode);
}

uint32_t Vni_Has128x32Animation(const Vni_Context* ctx) {
  if (!ctx) {
    return 0;
  }
  auto* context = reinterpret_cast<const Context*>(ctx);
  if (!context->pal || context->pal->masks.empty()) {
    return 0;
  }
  return context->pal->masks[0].size() == 512 ? 1 : 0;
}

uint32_t Vni_Colorize(Vni_Context* ctx, const uint8_t* frame, uint32_t width,
                      uint32_t height, uint8_t bitlen) {
  if (!ctx || !frame) {
    return 0;
  }
  auto* context = reinterpret_cast<Context*>(ctx);
  if (!context->pal || !context->palette) {
    return 0;
  }

  Dimensions dim(width, height);
  context->output.has_frame = false;

  if (bitlen == 4 && context->pal->palettes.size() > 1 && !context->vni) {
    if (frame[0] == 0x08 && frame[1] == 0x09 && frame[2] == 0x0a &&
        frame[3] == 0x0b) {
      uint32_t new_pal = static_cast<uint32_t>(frame[5]) * 8 + frame[4];
      if (static_cast<size_t>(new_pal) < context->pal->palettes.size()) {
        context->palette = &context->pal->palettes[new_pal];
        if (!context->palette->is_persistent()) {
          context->reset_embedded = true;
        }
        context->last_embedded_palette = static_cast<int>(new_pal);
      }
    } else if (context->reset_embedded) {
      if (context->default_palette) {
        context->palette = context->default_palette;
      }
      context->reset_embedded = false;
    }
  }

  auto planes = split_planes(frame, width, height, bitlen);

  if (!context->pal->mappings.empty()) {
    trigger_animation(context, dim, planes, false);
  }

  if (context->active_seq && context->active_seq->is_running) {
    render_animation(context, *context->active_seq, dim, planes);
  } else {
    render(context, dim, planes);
  }

  maybe_reset_palette(context);

  if (context->output.has_frame) {
    size_t colors = 1u << context->output.bitlen;
    context->output.palette = expand_palette(*context->palette, colors);
  }

  return context->output.has_frame ? 1 : 0;
}
