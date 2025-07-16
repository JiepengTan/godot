#!/bin/bash

# Godot MovieWriter 实时录制功能构建脚本

set -e  # 遇到错误时退出

echo "==============================================="
echo "开始编译 Godot 实时录制版本"
echo "==============================================="

# 构建配置
PLATFORM="macos"
TARGET="editor"
BUILD_TYPE="dev"

echo "平台: $PLATFORM"
echo "目标: $TARGET"  
echo "构建类型: $BUILD_TYPE"

# 编译命令
scons platform=macos target=editor
   

echo "==============================================="
echo "编译完成!"
echo "==============================================="

# 检查生成的二进制文件
BINARY_PATH="bin/godot.macos.editor.arm64"
if [ -f "$BINARY_PATH" ]; then
    echo "✅ 二进制文件生成成功: $BINARY_PATH"
    echo "文件大小: $(ls -lh $BINARY_PATH | awk '{print $5}')"
else
    echo "❌ 未找到预期的二进制文件: $BINARY_PATH"
    echo "可用的二进制文件:"
    ls -la bin/ || echo "bin/ 目录不存在"
    exit 1
fi

echo ""
echo "==============================================="
echo "测试项目位置: examples/realtime_recording_test/"
echo "运行测试: $BINARY_PATH --path examples/realtime_recording_test/"
echo "录制测试: $BINARY_PATH --path examples/realtime_recording_test/ --write-movie test.avi"
echo "===============================================" 
$BINARY_PATH --path examples/realtime_recording_test/ --write-movie test_fixed.avi --quit-after 100