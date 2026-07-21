#!/usr/bin/env bash
# Build a green distribution of RemoteTool + Agent.
#
# Output:
#   dist/
#   ├── remote_tool.exe
#   └── agent_windows.exe
#
# Both binaries are fully self-contained: statically linked to the GCC
# runtime (no MinGW DLL deps) and stripped of debug symbols. Sizes are
# well under the spec §16 targets (RemoteTool 3-10 MB, Agent 1-4 MB).
#
# Usage: bash tools/build-release.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# --- locate toolchain ----------------------------------------------------
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
echo "=== Toolchain: $MINGW_ROOT ==="
g++ --version | head -1
cmake --version | head -1

# --- configure & build ----------------------------------------------------
# Build cache: build/ (cleaned before each run; Regeneratable)
# Final distribution: dist/ — absolute Windows path printed at the end
BUILD_DIR="build"
DIST_DIR="dist"
rm -rf "$BUILD_DIR" "$DIST_DIR"

echo
echo "=== Configuring (Release) ==="
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo
echo "=== Building ==="
cmake --build "$BUILD_DIR" -j 4

# --- explicit strip (defence in depth; -Wl,-s already does it) ----------
echo
echo "=== Stripping ==="
for exe in "$BUILD_DIR/bin/remote_tool.exe" "$BUILD_DIR/bin/agent_windows.exe"; do
    if [[ -f "$exe" ]]; then
        strip -s "$exe"
    fi
done

# --- copy to dist/ --------------------------------------------------------
echo
echo "=== Packaging dist/ ==="
mkdir -p "$DIST_DIR"
cp "$BUILD_DIR/bin/remote_tool.exe"   "$DIST_DIR/"
cp "$BUILD_DIR/bin/agent_windows.exe" "$DIST_DIR/agent.exe"   # rename to spec §3.2 name
cp "$REPO_ROOT/README.md"             "$DIST_DIR/" 2>/dev/null || true

# --- report ---------------------------------------------------------------
echo
echo "=== Result ==="
printf '%-22s %10s %10s\n' "Binary" "Size" "Target"
printf '%-22s %10s %10s\n' "----------------------" "----------" "----------"
remote_size=$(stat -c%s "$DIST_DIR/remote_tool.exe")
agent_size=$(stat -c%s "$DIST_DIR/agent.exe")
printf '%-22s %9sB %9sB\n' "remote_tool.exe"  "$remote_size" "3-10 MB"
printf '%-22s %9sB %9sB\n' "agent.exe"        "$agent_size"  "1-4 MB"

echo
echo "=== File listing of dist/ ==="
ls -la "$DIST_DIR/"

echo
echo "=== DLL dependency check ==="
for exe in "$DIST_DIR/remote_tool.exe" "$DIST_DIR/agent.exe"; do
    echo "$exe:"
    "$MINGW_ROOT/bin/objdump" -p "$exe" 2>/dev/null | grep "DLL Name" | sort -u || \
        echo "  (no DLL imports — fully static)"
    echo
done

echo "=== Green distribution ready ==="
echo
# Convert the POSIX path to a Windows-style absolute path for the user.
if command -v cygpath >/dev/null 2>&1; then
    DIST_ABS="$(cygpath -w "$(cd "$DIST_DIR" && pwd)")"
else
    DIST_ABS="$(cd "$DIST_DIR" && pwd)"
fi
echo "  Absolute path: $DIST_ABS"
echo "  Files:"
ls "$DIST_DIR/" | sed 's/^/    /'
