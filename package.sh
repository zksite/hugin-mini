#!/usr/bin/env bash
# =====================================================================
# package.sh — 打包 libhugin_calib 动态库及其全部依赖
#
# 生成一个自包含的发布目录, 复制过去即可直接使用 (无需安装任何依赖):
#
#   dist/
#   ├── lib/
#   │   ├── libhugin_calib.so          # 标定引擎动态库 (本库)
#   │   ├── libpano13.so.3             # libpano13 (优化器核心)
#   │   ├── liblcms2.so.2              # lcms2 (色彩管理)
#   │   └── libvigraimpex.so.11        # VIGRA (图像基础库)
#   ├── include/
#   │   └── hugin_calib.h              # C API 头文件
#   ├── bin/
#   │   ├── hugin_calib_driver         # 命令行工具 (可选)
#   │   ├── example_c                  # C 示例
#   │   └── example_cpp                # C++ 示例
#   ├── examples/
#   │   ├── example_c.c                # 示例源码
#   │   ├── example_cpp.cpp
#   │   └── cp_sample.txt              # 示例控制点文件
#   └── README.md                      # 使用文档
#
# 用法:
#   ./package.sh [build_dir] [dist_dir]
#   默认: build_dir=build, dist_dir=dist
#
# 目标机器运行方式:
#   export LD_LIBRARY_PATH=$PWD/lib
#   ./bin/example_c examples/cp_sample.txt result.pto
# =====================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:-$SCRIPT_DIR/build}"
DIST_DIR="${2:-$SCRIPT_DIR/dist}"

echo "==> 打包 libhugin_calib"
echo "    build dir: $BUILD_DIR"
echo "    dist  dir: $DIST_DIR"

# ---- 1. 确保已构建 ----
if [ ! -f "$BUILD_DIR/libhugin_calib.so" ]; then
    echo "!! 未找到 $BUILD_DIR/libhugin_calib.so, 请先构建:"
    echo "   cd $BUILD_DIR && cmake .. && make -j2"
    exit 1
fi

# ---- 2. 创建目录结构 ----
mkdir -p "$DIST_DIR/lib" "$DIST_DIR/include" "$DIST_DIR/bin" "$DIST_DIR/examples"

# ---- 3. 复制本库 ----
cp -f "$BUILD_DIR/libhugin_calib.so" "$DIST_DIR/lib/"
echo "    copied libhugin_calib.so"

# ---- 4. 复制依赖动态库 (用 ldd 解析真实路径) ----
# 需要复制的依赖: libpano13, liblcms2, libvigraimpex
# (libstdc++/libc/libm 等系统库不复制, 目标机器必有)
copy_dep() {
    local soname="$1"
    local path
    path=$(ldd "$BUILD_DIR/libhugin_calib.so" | awk -v s="$soname" '$1 == s {print $3}')
    if [ -z "$path" ]; then
        # 尝试直接找
        path=$(find /usr/lib /lib /opt -name "$soname*" 2>/dev/null | head -1 || true)
    fi
    if [ -n "$path" ] && [ -f "$path" ]; then
        cp -fL "$path" "$DIST_DIR/lib/$(basename "$path")"
        echo "    copied $(basename "$path")"
    else
        echo "!! 警告: 找不到 $soname"
    fi
}

copy_dep "libpano13.so.3"
copy_dep "liblcms2.so.2"
copy_dep "libvigraimpex.so.11"

# ---- 5. 复制头文件 ----
cp -f "$SCRIPT_DIR/src/api/hugin_calib.h" "$DIST_DIR/include/"

# ---- 6. 复制可执行文件 (如果存在) ----
for exe in hugin_calib_driver example_c example_cpp verify_pto; do
    if [ -f "$BUILD_DIR/$exe" ]; then
        cp -f "$BUILD_DIR/$exe" "$DIST_DIR/bin/"
        echo "    copied bin/$exe"
    fi
done

# ---- 7. 复制示例源码 + 示例控制点 ----
cp -f "$SCRIPT_DIR/src/examples/example_c.c" "$DIST_DIR/examples/"
cp -f "$SCRIPT_DIR/src/examples/example_cpp.cpp" "$DIST_DIR/examples/"
cp -f "$SCRIPT_DIR/src/examples/example_python.py" "$DIST_DIR/examples/"
if [ -f "$SCRIPT_DIR/examples/cp_sample.txt" ]; then
    cp -f "$SCRIPT_DIR/examples/cp_sample.txt" "$DIST_DIR/examples/"
fi

# ---- 8. 复制文档 ----
cp -f "$SCRIPT_DIR/README.md" "$DIST_DIR/README.md" 2>/dev/null || true
cp -f "$SCRIPT_DIR/HUGIN_GUI_ALIGNMENT.md" "$DIST_DIR/HUGIN_GUI_ALIGNMENT.md" 2>/dev/null || true

echo ""
echo "==> 打包完成: $DIST_DIR"
echo ""
echo "在目标机器上使用:"
echo "  export LD_LIBRARY_PATH=$DIST_DIR/lib"
echo "  $DIST_DIR/bin/example_c $DIST_DIR/examples/cp_sample.txt result.pto"
echo ""
echo "或链接到你的程序:"
echo "  gcc myapp.c -I$DIST_DIR/include -L$DIST_DIR/lib -lhugin_calib -o myapp"
echo "  export LD_LIBRARY_PATH=$DIST_DIR/lib && ./myapp"