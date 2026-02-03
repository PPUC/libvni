cmake_minimum_required(VERSION 3.25)

set(PLATFORM "win" CACHE STRING "Platform")
set(ARCH "x64" CACHE STRING "Arch")

option(BUILD_SHARED "Option to build shared library" ON)
option(BUILD_STATIC "Option to build static library" ON)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan for Debug builds" OFF)

message(STATUS "PLATFORM: ${PLATFORM}")
message(STATUS "ARCH: ${ARCH}")

message(STATUS "BUILD_SHARED: ${BUILD_SHARED}")
message(STATUS "BUILD_STATIC: ${BUILD_STATIC}")
message(STATUS "ENABLE_SANITIZERS: ${ENABLE_SANITIZERS}")

if(PLATFORM STREQUAL "macos")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g")
  endif()
elseif(PLATFORM STREQUAL "ios" OR PLATFORM STREQUAL "ios-simulator")
  set(CMAKE_SYSTEM_NAME iOS)
  if (PLATFORM STREQUAL "ios-simulator")
    set(CMAKE_OSX_SYSROOT iphonesimulator)
  endif()
  set(CMAKE_OSX_DEPLOYMENT_TARGET 17.0)
elseif(PLATFORM STREQUAL "tvos")
  set(CMAKE_SYSTEM_NAME tvOS)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 17.0)
elseif(PLATFORM STREQUAL "android")
  set(CMAKE_SYSTEM_NAME Android)
  set(CMAKE_SYSTEM_VERSION 30)
  set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
endif()

file(READ src/vni-version.h version)
string(REGEX MATCH "VNI_VERSION_MAJOR[ ]+([0-9]+)" _tmp ${version})
set(VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "VNI_VERSION_MINOR[ ]+([0-9]+)" _tmp ${version})
set(VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "VNI_VERSION_PATCH[ ]+([0-9]+)" _tmp ${version})
set(VERSION_PATCH "${CMAKE_MATCH_1}")

project(vni VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
  LANGUAGES C CXX)

if(PLATFORM STREQUAL "win")
  if(ARCH STREQUAL "x86")
    add_compile_definitions(WIN32)
  endif()
elseif(PLATFORM STREQUAL "macos")
  if (ARCH STREQUAL "arm64")
    set(CMAKE_OSX_ARCHITECTURES arm64)
  elseif(ARCH STREQUAL "x64")
    set(CMAKE_OSX_ARCHITECTURES x86_64)
  endif()
  set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
  set(CMAKE_INSTALL_RPATH "@executable_path")
elseif(PLATFORM STREQUAL "ios" OR PLATFORM STREQUAL "ios-simulator" OR PLATFORM STREQUAL "tvos")
  set(CMAKE_OSX_DEPLOYMENT_TARGET 16.0)
  set(CMAKE_OSX_ARCHITECTURES arm64)
elseif(PLATFORM STREQUAL "linux" OR PLATFORM STREQUAL "android")
  set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
  set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(ENABLE_SANITIZERS AND (PLATFORM STREQUAL "macos" OR PLATFORM STREQUAL "linux"))
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)

    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
  endif()
endif()

set(VNI_SOURCES
  src/vni.cpp
  src/vni.h
  src/vni-version.h
  src/vni_internal.h
  src/vni_aes.cpp
  src/vni_aes.h
  src/vni_heatshrink.cpp
  src/vni_heatshrink.h
)

set(VNI_INCLUDE_DIRS
  src
  third-party/include
)

if(BUILD_SHARED)
  add_library(vni_shared SHARED ${VNI_SOURCES})
  target_include_directories(vni_shared PUBLIC ${VNI_INCLUDE_DIRS})
  target_compile_definitions(vni_shared PRIVATE VNI_EXPORTS)

  if(PLATFORM STREQUAL "win" AND ARCH STREQUAL "x64")
    set(VNI_OUTPUT_NAME "vni64")
  else()
    set(VNI_OUTPUT_NAME "vni")
  endif()

  set_target_properties(vni_shared PROPERTIES
    OUTPUT_NAME ${VNI_OUTPUT_NAME}
    VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
  )

  install(TARGETS vni_shared
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
  )
  install(FILES
    src/vni.h
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
endif()

if(BUILD_STATIC)
  add_library(vni_static STATIC ${VNI_SOURCES})
  target_include_directories(vni_static PUBLIC ${VNI_INCLUDE_DIRS})
  target_compile_definitions(vni_static PUBLIC VNI_STATIC)

  if(PLATFORM STREQUAL "win")
    set_target_properties(vni_static PROPERTIES OUTPUT_NAME "vni_static")
  else()
    set_target_properties(vni_static PROPERTIES OUTPUT_NAME "vni")
  endif()

  install(TARGETS vni_static
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
  )
  install(FILES
    src/vni.h
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
endif()
