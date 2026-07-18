#!/usr/bin/env bash
# MSVC CMake 构建脚本 —— 在 Git Bash 下手动配好 MSVC 环境并调用 cmake
# 用法:
#   bash tools/msvc-cmake.sh configure   # 配置（首次或 CMakeLists.txt 变更时）
#   bash tools/msvc-cmake.sh build       # 编译
#   bash tools/msvc-cmake.sh test        # 跑 CTest
#   bash tools/msvc-cmake.sh all         # configure + build + test
set -euo pipefail

# --- 定位 MSVC 与 Windows SDK (装在 D 盘) ---
MSVC_BASE="D:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
SDK_BASE="D:/Windows Kits/10"

MSVC_VER=$(ls "$MSVC_BASE" 2>/dev/null | sort -V | tail -1)
SDK_VER=$(ls "$SDK_BASE/Include" 2>/dev/null | sort -V | tail -1)

if [ -z "$MSVC_VER" ] || [ -z "$SDK_VER" ]; then
  echo "ERROR: 未找到 MSVC 或 Windows SDK"
  echo "  MSVC_BASE: $MSVC_BASE"
  echo "  SDK_BASE:  $SDK_BASE"
  exit 1
fi

MSVC="$MSVC_BASE/$MSVC_VER"
CL_DIR="$MSVC/bin/Hostx64/x64"
VSINSTALL="D:/Program Files/Microsoft Visual Studio/2022/Community"

# 设置完整的 MSVC 环境（等价于 vcvars64.bat 的关键变量）
export PATH="$CL_DIR:$SDK_BASE/bin/$SDK_VER/x64:/d/tools/mingw64/bin:$PATH"
export INCLUDE="$MSVC/include;$SDK_BASE/Include/$SDK_VER/ucrt;$SDK_BASE/Include/$SDK_VER/um;$SDK_BASE/Include/$SDK_VER/shared"
export LIB="$MSVC/lib/x64;$SDK_BASE/Lib/$SDK_VER/ucrt/x64;$SDK_BASE/Lib/$SDK_VER/um/x64"

# CMake 用这些变量定位 rc.exe / mt.exe / Windows SDK
export WindowsSdkDir="$SDK_BASE"
export VCToolsInstallDir="$MSVC/"
export VSINSTALLDIR="$VSINSTALL/"
export VCINSTALLDIR="$VSINSTALL/VC/"

# 强制 CMake 用 MSVC cl（用完整路径，避免 PATH 带空格的搜索问题）
export CC="$CL_DIR/cl.exe"
export CXX="$CL_DIR/cl.exe"

cd "$(dirname "$0")/.."

ACTION="${1:-all}"

case "$ACTION" in
  configure)
    echo "=== cmake configure (MSVC) ==="
    cmake --preset windows-x64-debug
    ;;
  build)
    echo "=== cmake build (MSVC) ==="
    cmake --build --preset windows-x64-debug
    ;;
  test)
    echo "=== ctest (MSVC) ==="
    ctest --preset windows-x64-debug --output-on-failure
    ;;
  all)
    echo "=== cmake configure (MSVC) ==="
    cmake --preset windows-x64-debug
    echo
    echo "=== cmake build (MSVC) ==="
    cmake --build --preset windows-x64-debug
    echo
    echo "=== ctest (MSVC) ==="
    ctest --preset windows-x64-debug --output-on-failure
    ;;
  *)
    echo "用法: bash tools/msvc-cmake.sh [configure|build|test|all]"
    exit 1
    ;;
esac

echo "=== done ==="
