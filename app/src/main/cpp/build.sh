#!/bin/bash

set -e

# 请根据你的 NDK 路径修改
NDK_PATH=~/Library/Android/sdk/ndk/27.0.12077973

# 编译输出目录
BUILD_DIR=build

# 目标架构，可根据需要改成 arm64-v8a、x86 等
ANDROID_ABI=arm64-v8a

# Android 最低平台版本
ANDROID_PLATFORM=android-21

echo "开始构建 native 库..."

mkdir -p $BUILD_DIR
cd $BUILD_DIR

cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$NDK_PATH/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=$ANDROID_ABI \
  -DANDROID_PLATFORM=$ANDROID_PLATFORM \
  -DCMAKE_BUILD_TYPE=Release

cmake --build .

echo "构建完成，输出目录：$(pwd)"

