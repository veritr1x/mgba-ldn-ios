#!/bin/sh
# Build the emulator/RFU core only. Use build.py to link it into the UIKit application.
set -eu
cd "$(dirname "$0")/../../.."
cmake -S . -B build/ios-core -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF \
  -DBUILD_STATIC=ON -DBUILD_SHARED=OFF \
  -DUSE_LDN_BROADCAST=OFF -DDISABLE_DEPS=ON \
  -DENABLE_SCRIPTING=OFF -DUSE_DISCORD_RPC=OFF \
  -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF \
  -DM_CORE_GB=OFF -DENABLE_DEBUGGERS=OFF -DENABLE_GDB_STUB=OFF \
  -DUSE_LZMA=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/ios-core --target mgba --parallel 8
