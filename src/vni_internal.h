#pragma once

#include <stdint.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vni {

struct Dimensions {
  uint32_t width = 128;
  uint32_t height = 32;

  Dimensions() = default;
  Dimensions(uint32_t w, uint32_t h) : width(w), height(h) {}

  uint32_t surface() const { return width * height; }
};

enum class SwitchMode : uint8_t {
  Palette = 0,
  Replace = 1,
  ColorMask = 2,
  Event = 3,
  Follow = 4,
  LayeredColorMask = 5,
  FollowReplace = 6,
  MaskedReplace = 7
};

struct Mapping {
  uint32_t checksum = 0;
  SwitchMode mode = SwitchMode::Palette;
  uint16_t palette_index = 0;
  uint32_t duration = 0;
  uint32_t offset = 0;

  bool is_animation() const {
    return mode != SwitchMode::Event && mode != SwitchMode::Palette;
  }
};

struct Palette {
  uint16_t index = 0;
  uint8_t type = 0;
  std::vector<uint8_t> colors;  // RGB triples

  bool is_default() const { return type == 1 || type == 2; }
  bool is_persistent() const { return type == 1; }
};

struct AnimationPlane {
  uint8_t marker = 0;
  std::vector<uint8_t> plane;
};

struct AnimationFrame {
  uint32_t time = 0;
  uint32_t delay = 0;
  uint8_t bit_length = 0;
  std::vector<AnimationPlane> planes;
  std::vector<uint8_t> mask;
  uint32_t hash = 0;
};

struct FrameSeq {
  std::string name;
  uint32_t offset = 0;
  std::vector<AnimationFrame> frames;
  uint32_t animation_duration = 0;
  Dimensions size;
  SwitchMode switch_mode = SwitchMode::Palette;

  std::vector<std::vector<uint8_t>> masks;

  bool is_running = false;

  uint32_t frame_index = 0;
  int64_t last_tick = 0;
  int64_t timer = 0;

  std::vector<std::vector<uint8_t>> lcm_buffer_planes;
  std::vector<uint8_t> replace_mask;
};

struct VniFile {
  uint16_t version = 0;
  std::vector<FrameSeq> animations;
  Dimensions dimensions;
};

struct PalFile {
  uint8_t version = 0;
  std::vector<Palette> palettes;
  std::map<uint32_t, Mapping> mappings;
  std::vector<std::vector<uint8_t>> masks;
  int default_palette_index = -1;
};

struct OutputFrame {
  std::vector<uint8_t> data;
  std::vector<uint8_t> palette;
  Dimensions dimensions;
  uint8_t bitlen = 0;
  bool has_frame = false;
};

enum class ScalerMode : uint32_t {
  None = 0,
  Scale2x = 1,
  ScaleDouble = 2,
};

struct Context {
  std::unique_ptr<VniFile> vni;
  std::unique_ptr<PalFile> pal;
  OutputFrame output;
  ScalerMode scaler_mode = ScalerMode::None;

  FrameSeq* active_seq = nullptr;
  Palette* palette = nullptr;
  Palette* default_palette = nullptr;
  int last_embedded_palette = -1;
  bool reset_embedded = false;
  int64_t palette_reset_at = -1;

  uint32_t tick() const;
};

}  // namespace vni
