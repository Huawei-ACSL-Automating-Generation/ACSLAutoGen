#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$ROOT_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_COMPILER=gcc-13 \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_C_COMPILER_AR=gcc-ar-13 \
  -DCMAKE_C_COMPILER_RANLIB=gcc-ranlib-13 \
  -DCMAKE_CXX_COMPILER_AR=gcc-ar-13 \
  -DCMAKE_CXX_COMPILER_RANLIB=gcc-ranlib-13 \
  "$ROOT_DIR"
cmake --build . --parallel $(nproc)  
echo "Build completed successfully."
