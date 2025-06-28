@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

:: 设置 NDK 路径（请根据你的路径修改）
set NDK_PATH=F:\AndroidSDK\ndk\27.0.12077973

:: 设置目标 ABI（可修改为 armeabi-v7a, x86 等）
set ANDROID_ABI=arm64-v8a

:: 设置 Android 最低平台版本
set ANDROID_PLATFORM=android-21

:: 设置构建目录
set BUILD_DIR=build

echo [*] Cleaning old build...
if exist %BUILD_DIR%\CMakeCache.txt (
    del /f /q %BUILD_DIR%\CMakeCache.txt
)
if exist %BUILD_DIR%\CMakeFiles (
    rmdir /s /q %BUILD_DIR%\CMakeFiles
)

echo [*] Starting build...

echo 正在构建 native 库...

if not exist %BUILD_DIR% (
    mkdir %BUILD_DIR%
)
cd %BUILD_DIR%

cmake .. ^
 -G "Ninja" ^
 -DCMAKE_TOOLCHAIN_FILE=%NDK_PATH%\build\cmake\android.toolchain.cmake ^
 -DANDROID_ABI=%ANDROID_ABI% ^
 -DANDROID_PLATFORM=%ANDROID_PLATFORM% ^
 -DCMAKE_BUILD_TYPE=Release
 -G "Ninja"


cmake --build .

echo 构建完成，输出目录：%CD%

pause
