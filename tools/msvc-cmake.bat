@echo off
REM MSVC CMake 构建脚本 —— 在 cmd.exe 里运行（非 Git Bash）
REM 用法: tools\msvc-cmake.bat [configure|build|test|all]
REM 这个脚本会先调用 vcvars64.bat 初始化 MSVC 环境，再跑 cmake

setlocal

set VSBAT=D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat

if not exist "%VSBAT%" (
    echo ERROR: vcvars64.bat 未找到: %VSBAT%
    exit /b 1
)

call "%VSBAT%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvars64.bat 初始化失败
    exit /b 1
)

cd /d D:\coding\RemoteTool

set ACTION=%1
if "%ACTION%"=="" set ACTION=all

if "%ACTION%"=="configure" goto configure
if "%ACTION%"=="build" goto build
if "%ACTION%"=="test" goto test
if "%ACTION%"=="all" goto all

echo 用法: tools\msvc-cmake.bat [configure^|build^|test^|all]
exit /b 1

:configure
echo === cmake configure (MSVC) ===
cmake --preset windows-x64-debug
goto done

:build
echo === cmake build (MSVC) ===
cmake --build --preset windows-x64-debug
goto done

:test
echo === ctest (MSVC) ===
ctest --preset windows-x64-debug --output-on-failure
goto done

:all
echo === cmake configure (MSVC) ===
cmake --preset windows-x64-debug
echo.
echo === cmake build (MSVC) ===
cmake --build --preset windows-x64-debug
echo.
echo === ctest (MSVC) ===
ctest --preset windows-x64-debug --output-on-failure
goto done

:done
echo === done ===
endlocal
