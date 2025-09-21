#!/bin/bash

# 如果任何命令失败，立即退出
set -e

# 使用相对路径，更简洁。-p 选项表示如果目录已存在则不报错
mkdir -p build

# 清理旧的构建文件
rm -rf build/*

# 进入build目录，执行cmake和make
cd build
cmake ..
make -j$(nproc) # 使用 nproc 自动获取CPU核心数，更通用

# 回到项目根目录
cd ..

# --- 安装部分：这部分通常需要 sudo 权限 ---
echo "Installing headers and library to system directories (requires sudo)..."

# 创建安装目录
sudo mkdir -p /usr/include/mymuduo

# 拷贝所有头文件
# 使用 find 命令更安全，可以处理各种文件名
# -maxdepth 1 确保只查找当前目录的头文件
sudo find . -maxdepth 1 -name "*.h" -exec cp {} /usr/include/mymuduo/ \;

# 拷贝共享库
# 假设库文件在 build/lib 目录下，这是更常见的CMake配置
if [ -f build/lib/libmymuduo.so ]; then
    sudo cp build/lib/libmymuduo.so /usr/lib/
else
    echo "Error: libmymuduo.so not found in build/lib/"
    exit 1
fi

# 更新动态链接器缓存
echo "Updating dynamic linker cache..."
sudo ldconfig

echo "Build and installation complete!"