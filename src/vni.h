#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(VNI_STATIC)
#define VNI_API
#elif defined(_MSC_VER)
#if defined(VNI_EXPORTS)
#define VNI_API __declspec(dllexport)
#else
#define VNI_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#if defined(VNI_EXPORTS)
#define VNI_API __attribute__((visibility("default")))
#else
#define VNI_API
#endif
#else
#define VNI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Vni_Context Vni_Context;

typedef struct Vni_Frame_Struc {
  uint32_t width;
  uint32_t height;
  uint8_t bitlen;
  uint8_t has_frame;
  const uint8_t* frame;    // indexed pixels, size = width * height
  const uint8_t* palette;  // RGB triples, size = (1 << bitlen) * 3
} Vni_Frame_Struc;

// Loads PAL/VNI data from the provided paths. Any path may be null.
// pac_path and vni_key are accepted for API compatibility, but encrypted PAC
// files are not supported. If pac_path is provided, an error is logged and it
// is ignored.
VNI_API Vni_Context* Vni_LoadFromPaths(const char* pal_path,
                                       const char* vni_path,
                                       const char* pac_path,
                                       const char* vni_key);

// Releases all resources held by the context.
VNI_API void Vni_Dispose(Vni_Context* ctx);

// Returns a pointer to the current output frame buffer.
VNI_API const Vni_Frame_Struc* Vni_GetFrame(const Vni_Context* ctx);

// Sets the scaler mode: 0 = none, 1 = scale2x, 2 = doubled pixels.
VNI_API void Vni_SetScalerMode(Vni_Context* ctx, uint32_t mode);

// Returns 1 if the PAL file contains 128x32 masks (used by some consumers).
VNI_API uint32_t Vni_Has128x32Animation(const Vni_Context* ctx);

// Colorizes a frame. Input is indexed pixels (0..(2^bitlen-1)).
// Returns 1 if an output frame is available.
VNI_API uint32_t Vni_Colorize(Vni_Context* ctx, const uint8_t* frame,
                              uint32_t width, uint32_t height, uint8_t bitlen);

#ifdef __cplusplus
}  // extern "C"
#endif
