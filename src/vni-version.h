#pragma once

#define VNI_VERSION_MAJOR 0  // X Digits
#define VNI_VERSION_MINOR 1  // Max 2 Digits
#define VNI_VERSION_PATCH 0  // Max 2 Digits

#define _VNI_STR(x) #x
#define VNI_STR(x) _VNI_STR(x)

#define VNI_VERSION            \
  VNI_STR(VNI_VERSION_MAJOR) \
  "." VNI_STR(VNI_VERSION_MINOR) "." VNI_STR(VNI_VERSION_PATCH)
#define VNI_MINOR_VERSION \
  VNI_STR(VNI_VERSION_MAJOR) "." VNI_STR(VNI_VERSION_MINOR)
