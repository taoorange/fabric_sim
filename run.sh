#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
TARGET="$BUILD_DIR/fabric"

if ! command -v cmake >/dev/null 2>&1; then
  echo "Error: cmake not found. Install it first: brew install cmake" >&2
  exit 1
fi

echo "[1/3] Configuring project..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR"

echo "[2/3] Building project..."
cmake --build "$BUILD_DIR" -j

if [[ ! -x "$TARGET" ]]; then
  echo "Error: build succeeded but executable not found: $TARGET" >&2
  exit 1
fi

echo "[3/3] Running simulator..."
exec "$TARGET"

