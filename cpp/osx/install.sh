#!/usr/bin/env bash
# Install build dependencies for mono-cpp on macOS.
# Assumes Homebrew is already installed (https://brew.sh).
#
# Usage (on the Mac or in the guest):
#   chmod +x osx/install.sh
#   ./osx/install.sh

set -euo pipefail

# Non-login SSH and some GUIs have a tiny PATH; Homebrew lives here on Intel vs Apple Silicon.
export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/usr/sbin:/sbin${PATH+:$PATH}"

if ! command -v brew >/dev/null 2>&1; then
  echo "install.sh: Homebrew not found. Install it first: https://brew.sh" >&2
  exit 1
fi

# Apple Command Line Tools (compiler, SDK) — required for cmake to find a C++ toolchain.
if ! xcode-select -p &>/dev/null; then
  echo "install.sh: Apple Command Line Tools are not selected." >&2
  echo "  Run:  xcode-select --install" >&2
  echo "  Then re-run this script." >&2
  exit 1
fi

echo "==> Homebrew: installing cmake, ninja"
brew install cmake ninja

echo ""
echo "Done. Configure and build:"
echo "  cmake --preset release-macos -G Ninja"
echo "  cmake --build --preset release-macos"
