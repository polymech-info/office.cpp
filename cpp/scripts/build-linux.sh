#!/bin/bash
# Build script for Linux (native or cross-compile)
# Usage: ./scripts/build-linux.sh

set -euo pipefail

cd "$(dirname "$0")/.."

echo "==> Configuring for Linux..."
cmake --preset release-linux -G Ninja

echo "==> Building..."
cmake --build --preset release-linux

echo ""
echo "==> Build complete. Binary at: ./dist/mono-cpp"
echo "==> Run with: ./dist/mono-cpp --help"
