A simple platform-independent conversion to C++ of the VNI converter of LibDmd which written in C#, keeping the same license GPLv2.
LibDmd is part of [DMD Extensions](https://github.com/freezy/dmd-extensions).

The purpose of this lib is to support unprotected VNI colorizations in libdmdutil, mainly to create RGB565 dumps of colorized frames as a jump-start for the author of the VNI to convert the project into a Serum colorization.

## API

`src/vni.h`: `Vni_LoadFromPaths` loads a PAL file and its VNI file (encrypted PAC
files are not supported), `Vni_Colorize` colorizes a frame of 2 or 4 bits and
`Vni_GetFrame` returns the result with its palette. `Vni_SetPalette` selects a
palette directly. `Vni_SetTime` (0.2.0) makes animations and palette durations
go by the times the caller gives instead of the system clock, so that recorded
frames (a dump, the frames of an editor) can be colorized faster than they were
shown.

## Origin

- `src/vni.cpp` and `src/vni_frameutil.h` (the bit plane and checksum helpers,
  0.2.1) were converted from LibDmd's VNI colorizer and its `FrameUtil.cs`.
- `src/vni_heatshrink.cpp` decodes heatshrink-compressed VNI animations, as
  LibDmd's decoder does, a port of
  [atomicobject's heatshrink](https://github.com/atomicobject/heatshrink) (ISC
  license).
- Splitting frames into bit planes, scaling and centring come from
  [libframeutil](https://github.com/PPUC/libframeutil).

## Building:

#### Windows (x64)

```shell
platforms/win/x64/external.sh
cmake -G "Visual Studio 18 2026" -DPLATFORM=win -DARCH=x64 -B build
cmake --build build --config Release
```

#### Windows (x86)

```shell
platforms/win/x86/external.sh
cmake -G "Visual Studio 18 2026" -A Win32 -DPLATFORM=win -DARCH=x86 -B build
cmake --build build --config Release
```

#### Windows MinGW / MSYS2 UCRT64 (x64)

Requires MSYS2 with UCRT64 environment. Install dependencies:

```shell
pacman -S --noconfirm \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake
```

Build (entire build runs inside the MSYS2 UCRT64 shell):

```shell
MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -l -c "
  cd \"$(pwd)\" &&
  ./platforms/win-mingw/x64/external.sh &&
  cmake -DCMAKE_BUILD_TYPE=Release -DPLATFORM=win-mingw -DARCH=x64 -B build &&
  cmake --build build -- -j\$(nproc)
"
```

#### Linux (x64)
```shell
platforms/linux/x64/external.sh
cmake -DPLATFORM=linux -DARCH=x64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### Linux (aarch64)
```shell
platforms/linux/aarch64/external.sh
cmake -DPLATFORM=linux -DARCH=aarch64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### MacOS (arm64)
```shell
platforms/macos/arm64/external.sh
cmake -DPLATFORM=macos -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### MacOS (x64)
```shell
platforms/macos/x64/external.sh
cmake -DPLATFORM=macos -DARCH=x64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### iOS (arm64)
```shell
platforms/ios/arm64/external.sh
cmake -DPLATFORM=ios -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### iOS Simulator (arm64)
```shell
platforms/ios-simulator/arm64/external.sh
cmake -DPLATFORM=ios-simulator -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### tvOS (arm64)
```shell
platforms/tvos/arm64/external.sh
cmake -DPLATFORM=tvos -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### Android (arm64-v8a)
```shell
platforms/android/arm64-v8a/external.sh
cmake -DPLATFORM=android -DARCH=arm64-v8a -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```
