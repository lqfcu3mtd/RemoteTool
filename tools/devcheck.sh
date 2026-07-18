#!/usr/bin/env bash
# Local dev sanity check for platform-independent core layer.
# Uses a no-admin, extract-and-run MinGW-w64 GCC toolchain.
#
# Default toolchain search order (override with MINGW_ROOT env var):
#   1. $MINGW_ROOT/bin
#   2. D:/tools/mingw64/bin        (preferred: D drive, per user request)
#   3. C:/Users/18385/tools/mingw64/bin  (legacy fallback)
#
# Run from repo root:
#   bash tools/devcheck.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# --- locate g++ ----------------------------------------------------------
MINGW_ROOT="${MINGW_ROOT:-}"
if [[ -n "$MINGW_ROOT" && -x "$MINGW_ROOT/bin/g++.exe" ]]; then
  : # user override wins
elif [[ -x "/d/tools/mingw64/bin/g++.exe" ]]; then
  MINGW_ROOT="/d/tools/mingw64"
elif [[ -x "/c/Users/18385/tools/mingw64/bin/g++.exe" ]]; then
  MINGW_ROOT="/c/Users/18385/tools/mingw64"
else
  echo "ERROR: g++.exe not found. Set MINGW_ROOT or install MinGW to D:/tools/mingw64" >&2
  exit 1
fi
export PATH="$MINGW_ROOT/bin:$PATH"

echo "Using toolchain: $MINGW_ROOT"
g++ --version | head -1

# --- compile + run frame unit test --------------------------------------
mkdir -p build
g++ -std=c++17 -Wall -Wextra -O0 -g \
  src/protocol/frame.cpp \
  tests/unit/frame_test.cpp \
  -I include \
  -o build/frame_test

echo "--- running frame_test ---"
./build/frame_test
echo "frame_test exit: $?"
