#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/ios-tests
xcrun --sdk macosx clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DM_CORE_GBA -DUSE_PTHREADS -DENABLE_VFS -DENABLE_VFS_FD -DENABLE_DIRECTORIES \
  -Iinclude -Ibuild/ios-core/include -Isrc -Isrc/gba/sio/ldn -Isrc/platform/ios-ldn \
  src/platform/ios-ldn/protocol-test.c src/platform/ios-ldn/apple-crypto.c \
  src/platform/ios-ldn/relay-backend.c src/platform/ios-ldn/relay/relay_codec.c \
  src/gba/sio/ldn/ldn-pia.c src/gba/sio/ldn/ldn-pia-connect.c src/gba/sio/ldn/ldn-pia-reliable.c \
  src/gba/sio/ldn/trade-shim.c src/third-party/zstd/zstdlib.c src/util/crc32.c \
  -o build/ios-tests/protocol-test
build/ios-tests/protocol-test
