#!/usr/bin/env bash
set -euo pipefail

# Build and run all tests with one command.
# Usage: ./run-tests.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT_DIR/build-tests"}"

echo ">> Build directory: $BUILD_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_TESTS=ON

CPU_CORES=1
if command -v nproc >/dev/null 2>&1; then
  CPU_CORES="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
  CPU_CORES="$(sysctl -n hw.ncpu)"
fi

cmake --build "$BUILD_DIR" --target test_all --parallel "$CPU_CORES"

pushd "$BUILD_DIR" >/dev/null
if ctest --output-on-failure --no-tests=error; then
  echo "✅ All tests passed"
else
  echo "❌ Tests failed"
  exit 1
fi
popd >/dev/null
