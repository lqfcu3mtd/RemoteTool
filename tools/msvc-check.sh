#!/usr/bin/env bash
# MSVC 生产构建验证脚本 —— 在 Git Bash 下手动配好 MSVC 环境并编译运行 frame_test
# 用法: bash tools/msvc-check.sh
# 依赖: VS2022 Community 装在 D:\Program Files\..., Windows 11 SDK 装在 D:\Windows Kits\10
set -euo pipefail

# --- 定位 MSVC 与 Windows SDK (装在 D 盘) ---
MSVC_BASE="D:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
SDK_BASE="D:/Windows Kits/10"

# 自动取 MSVC 最高版本
MSVC_VER=$(ls "$MSVC_BASE" 2>/dev/null | sort -V | tail -1)
if [ -z "$MSVC_VER" ]; then
  echo "ERROR: MSVC 未安装在 $MSVC_BASE"; exit 1
fi
MSVC="$MSVC_BASE/$MSVC_VER"

# 自动取 Windows SDK 最高版本
SDK_VER=$(ls "$SDK_BASE/Include" 2>/dev/null | sort -V | tail -1)
if [ -z "$SDK_VER" ]; then
  echo "ERROR: Windows SDK 未安装在 $SDK_BASE"; exit 1
fi

CL="$MSVC/bin/Hostx64/x64/cl.exe"
export INCLUDE="$MSVC/include;$SDK_BASE/Include/$SDK_VER/ucrt;$SDK_BASE/Include/$SDK_VER/um;$SDK_BASE/Include/$SDK_VER/shared"
export LIB="$MSVC/lib/x64;$SDK_BASE/Lib/$SDK_VER/ucrt/x64;$SDK_BASE/Lib/$SDK_VER/um/x64"

cd "$(dirname "$0")/.."
mkdir -p build-msvc

echo "=== MSVC 生产构建验证 ==="
echo "  cl.exe    : $CL"
echo "  MSVC ver  : $MSVC_VER"
echo "  SDK ver   : $SDK_VER"
echo

echo "[1] 编译 frame_test (MSVC x64, /O2)"
"$CL" /nologo /EHsc /std:c++17 /W3 /O2 \
  /D_CRT_SECURE_NO_WARNINGS \
  /I include \
  src/protocol/frame.cpp tests/unit/frame_test.cpp \
  /Fe:build-msvc/frame_test.exe \
  /Fo:build-msvc/ 2>&1
echo

echo "[2] 运行 frame_test (MSVC 构建)"
./build-msvc/frame_test.exe
echo

echo "=== MSVC 验证完成 ==="
