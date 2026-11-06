#!/bin/bash
# Install build dependencies for Linux (Debian/Ubuntu)
# Usage: ./scripts/install-lnx.sh

set -euo pipefail

echo "==> Installing build dependencies..."

if command -v apt-get >/dev/null 2>&1; then
    # Debian/Ubuntu
    sudo apt-get update
    sudo apt-get install -y build-essential cmake ninja-build clang
elif command -v yum >/dev/null 2>&1; then
    # RHEL/CentOS/Fedora
    sudo yum groupinstall -y "Development Tools"
    sudo yum install -y cmake ninja-build clang
elif command -v pacman >/dev/null 2>&1; then
    # Arch Linux
    sudo pacman -S --needed base-devel cmake ninja clang
else
    echo "Unknown package manager. Please install: cmake, ninja-build, clang, build-essential"
    exit 1
fi

echo ""
echo "Done. Build with: npm run build:linux"
