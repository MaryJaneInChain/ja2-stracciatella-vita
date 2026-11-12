#!/bin/sh
# Build ja2-stracciatella.vpk for PS Vita inside the container (see Containerfile).
# First time: podman build --network=host -t stracciatella-vita-sdk vita
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
vita/run.sh 'export CARGO_UNSTABLE_BUILD_STD=std,panic_abort
[ -f build-vita/build.ninja ] || cmake -S . -B build-vita -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DCMAKE_BUILD_TYPE=Release \
  -DCARGO_BUILD_TARGET=armv7-sony-vita-newlibeabihf \
  -DBUILD_LAUNCHER=OFF -DWITH_UNITTESTS=OFF -DWITH_RUST_BINARIES=OFF -DLOCAL_GTEST_LIB=OFF
cmake --build build-vita'
ls -la build-vita/ja2-stracciatella.vpk
