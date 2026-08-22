# libhugin_calib — Hugin 最小化标定引擎动态库

基于 Hugin 源码裁剪的**共享镜头标定引擎**，封装为自包含动态库。
吃控制点（两两相机对应点），输出 AVS 兼容的 pto 几何参数
（`v/a/b/c` 共享镜头 + 每图 `y/p/r` 姿态）。

**核心特性**：

- ✅ **纯 C API** — 可被 C / C++ / Python(ctypes) / Rust / Go 等任意语言调用
- ✅ **自包含** — 所有依赖（libpano13 / lcms2 / VIGRA）已打包，复制即用
- ✅ **完全复现 Hugin GUI** — `AUTO | FIX_LENS | DISTRIBUTE` 三标志组合
  与 Hugin GUI "位置（增量，从锚定点开始）" 结果一致（误差 < 0.001°）
- ✅ **线程安全** — 每个句柄独立，无全局可变状态
- ✅ **无头环境可用** — 不依赖 GUI / OpenGL / EXIF 库

---

## 1. 目录结构

```
dist/
├── lib/
│   ├── libhugin_calib.so          # 标定引擎动态库 (本库)
│   ├── libpano13.so.3             # libpano13 (LM 优化器核心)
│   ├── liblcms2.so.2              # lcms2 (色彩管理)
│   └── libvigraimpex.so.11        # VIGRA (图像基础库)
├── include/
│   └── hugin_calib.h              # C API 头文件 (唯一需要 include 的文件)
├── bin/
│   ├── hugin_calib_driver         # 命令行工具 (可选)
│   ├── example_c                  # C 示例 (已编译)
│   ├── example_cpp                # C++ 示例 (已编译)
│   └── verify_pto                 # pto 验证工具 (可选)
├── examples/
│   ├── example_c.c                # C 示例源码
│   ├── example_cpp.cpp            # C++ 示例源码
│   ├── example_python.py          # Python ctypes 示例
│   └── cp_sample.txt              # 示例控制点文件
└── README.md                      # 本文档
```

---

## 2. 快速开始

### 2.1 复制到目标机器

```bash
# 整个 dist 目录复制过去即可, 无需安装任何依赖
scp -r dist/ user@target:/opt/hugin_calib/
```

### 2.2 运行示例

```bash
cd /opt/hugin_calib
export LD_LIBRARY_PATH=$PWD/lib

# C 示例 (4 路相机, 从文件加载控制点)
./bin/example_c examples/cp_sample.txt result.pto

# Python 示例 (3 路相机)
python3 examples/example_python.py lib/libhugin_calib.so examples/cp_sample.txt
```

### 2.3 链接到你的程序

```bash
# C
gcc myapp.c -I/opt/hugin_calib/include -L/opt/hugin_calib/lib -lhugin_calib -o myapp

# C++
g++ myapp.cpp -I/opt/hugin_calib/include -L/opt/hugin_calib/lib -lhugin_calib -o myapp

# 运行
export LD_LIBRARY_PATH=/opt/hugin_calib/lib
./myapp
```

---

## 3. C API 参考

头文件：`include/hugin_calib.h`（唯一需要 include 的文件）

### 3.1 生命周期

```c
hugin_calib_handle *hugin_calib_create(void);          // 创建句柄
void hugin_calib_destroy(hugin_calib_handle *h);       // 销毁句柄
```

### 3.2 配置

```c
// 设置相机参数 (所有图像共享同一镜头)
int hugin_calib_set_images(hugin_calib_handle *h,
                           int width, int height,      // 图像尺寸 (px)
                           int proj,                   // 投影类型 (见下)
                           double hfov,                // 初始 HFOV (度)
                           double crop,                // crop factor (仅元数据)
                           int num_images);            // 相机数量 (>= 2)

// 设置第 idx 张图像的初始姿态 (度, 默认全 0)
int hugin_calib_set_initial_pose(hugin_calib_handle *h, int idx,
                                 double yaw, double pitch, double roll);

// 从文件加载初始姿态 (每行 "yaw pitch roll", 一行一张图)
int hugin_calib_load_initial_poses(hugin_calib_handle *h, const char *filename);

// 设置优化行为标志 (可组合)
int hugin_calib_set_flags(hugin_calib_handle *h, int flags);

// 设置第 idx 张图像的优化变量 (逗号分隔, 如 "y,p,r")
int hugin_calib_set_optimize_vars(hugin_calib_handle *h, int idx, const char *vars);
```

**投影类型** (`hugin_calib_projection`)：

| 值 | 名称 | 说明 |
|---|---|---|
| 0 | `HUGIN_CALIB_PROJ_RECTILINEAR` | 直线投影 (普通镜头) |
| 2 | `HUGIN_CALIB_PROJ_CIRCULAR_FISHEYE` | 圆形鱼眼 |
| **3** | `HUGIN_CALIB_PROJ_FULL_FRAME_FISHEYE` | **全帧鱼眼 (4K 相机常用)** |
| 4 | `HUGIN_CALIB_PROJ_EQUIRECTANGULAR` | 等距柱状 |
| 8 | `HUGIN_CALIB_PROJ_FISHEYE_ORTHOGRAPHIC` | 正射鱼眼 |
| 10 | `HUGIN_CALIB_PROJ_FISHEYE_STEREOGRAPHIC` | 立体鱼眼 |
| 20 | `HUGIN_CALIB_PROJ_FISHEYE_THOBY` | Thoby 鱼眼 |
| 21 | `HUGIN_CALIB_PROJ_FISHEYE_EQUISOLID` | 等立体角鱼眼 |

**优化标志** (`hugin_calib_flags`)：

| 标志 | 值 | 说明 |
|---|---|---|
| `HUGIN_CALIB_FLAG_AUTO` | 1 | 增量逐对预优化 (对应 Hugin "位置（增量）") |
| `HUGIN_CALIB_FLAG_FIX_LENS` | 2 | 固定镜头参数, 不优化 v/a/b/c |
| `HUGIN_CALIB_FLAG_DISTRIBUTE` | 4 | 自动均匀分布初始 yaw (对应 DistributeImagesCmd) |

> **完全复现 Hugin GUI "位置（增量，从锚定点开始）"**：
> `AUTO | FIX_LENS | DISTRIBUTE` 三标志组合。

### 3.3 控制点

```c
// 逐点添加 (两图对应点, 0-based 相机编号, 像素坐标)
int hugin_calib_add_cp(hugin_calib_handle *h,
                       int img1, double x1, double y1,
                       int img2, double x2, double y2);

// 从文件加载 (每行 "img1 x1 y1 img2 x2 y2", '#' 开头行忽略)
int hugin_calib_load_cp_file(hugin_calib_handle *h, const char *filename);

int hugin_calib_get_num_cp(const hugin_calib_handle *h);  // 控制点数量
```

### 3.4 执行与结果

```c
int hugin_calib_run(hugin_calib_handle *h);   // 运行优化

// 结果结构
typedef struct hugin_calib_image_result {
    int    width, height, projection;
    double v, a, b, c;        // 镜头参数 (共享)
    double yaw, pitch, roll;  // 姿态 (度)
    char   filename[256];
} hugin_calib_image_result;

typedef struct hugin_calib_result {
    int    num_images;
    double rms_error;                    // 优化后 RMS 重投影误差 (px)
    hugin_calib_image_result *images;    // 库分配, 调用方 free()
} hugin_calib_result;

int hugin_calib_get_result(const hugin_calib_handle *h, hugin_calib_result *result);
int hugin_calib_write_pto(const hugin_calib_handle *h, const char *filename);  // 写 AVS 兼容 pto
const char *hugin_calib_last_error(const hugin_calib_handle *h);               // 错误消息
```

### 3.5 工具函数

```c
// 计算全帧鱼眼 HFOV (度): 焦距 2.95mm, crop 4.85, 3840x2160 → 151.013868°
double hugin_calib_calc_hfov(double focal_mm, double crop_factor, int width, int height);

const char *hugin_calib_version(void);   // 版本字符串
```

### 3.6 返回码

| 值 | 名称 | 含义 |
|---|---|---|
| 0 | `HUGIN_CALIB_OK` | 成功 |
| -1 | `HUGIN_CALIB_ERR_NULL` | 空指针参数 |
| -2 | `HUGIN_CALIB_ERR_INVALID` | 参数非法 |
| -3 | `HUGIN_CALIB_ERR_CP_FILE` | 控制点文件无法打开 |
| -4 | `HUGIN_CALIB_ERR_NO_CP` | 没有有效控制点 |
| -5 | `HUGIN_CALIB_ERR_OPTIMIZE` | 优化器失败 |
| -6 | `HUGIN_CALIB_ERR_WRITE` | 输出文件写入失败 |
| -7 | `HUGIN_CALIB_ERR_INIT_FILE` | 初始姿态文件无法打开 |
| -8 | `HUGIN_CALIB_ERR_NOT_RUN` | 尚未运行优化 |

---

## 4. 完整示例

### 4.1 C 语言

```c
#include <stdio.h>
#include <stdlib.h>
#include "hugin_calib.h"

int main(void)
{
    hugin_calib_handle *h = hugin_calib_create();
    if (!h) return 1;

    // 4K 全帧鱼眼, 焦距 2.95mm, crop 4.85, 4 路相机
    double hfov = hugin_calib_calc_hfov(2.95, 4.85, 3840, 2160);
    hugin_calib_set_images(h, 3840, 2160,
                           HUGIN_CALIB_PROJ_FULL_FRAME_FISHEYE,
                           hfov, 4.85, 4);

    // 加载控制点
    hugin_calib_load_cp_file(h, "cp.txt");

    // 复现 Hugin GUI "位置（增量，从锚定点开始）"
    hugin_calib_set_flags(h, HUGIN_CALIB_FLAG_AUTO |
                             HUGIN_CALIB_FLAG_FIX_LENS |
                             HUGIN_CALIB_FLAG_DISTRIBUTE);

    // 运行
    int ret = hugin_calib_run(h);
    if (ret != HUGIN_CALIB_OK) {
        fprintf(stderr, "run failed: %s\n", hugin_calib_last_error(h));
        return 1;
    }

    // 读取结果
    hugin_calib_result result;
    hugin_calib_get_result(h, &result);
    printf("RMS: %.4f px\n", result.rms_error);
    for (int i = 0; i < result.num_images; ++i) {
        printf("cam%d: v=%.6f y=%.6f p=%.6f r=%.6f\n", i,
               result.images[i].v, result.images[i].yaw,
               result.images[i].pitch, result.images[i].roll);
    }
    free(result.images);   // 库分配, 调用方释放

    // 写 pto
    hugin_calib_write_pto(h, "result.pto");

    hugin_calib_destroy(h);
    return 0;
}
```

### 4.2 Python (ctypes)

```python
import ctypes

lib = ctypes.CDLL("lib/libhugin_calib.so")
lib.hugin_calib_create.restype = ctypes.c_void_p
lib.hugin_calib_set_images.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                       ctypes.c_int, ctypes.c_double, ctypes.c_double,
                                       ctypes.c_int]
lib.hugin_calib_run.argtypes = [ctypes.c_void_p]
lib.hugin_calib_run.restype = ctypes.c_int

h = lib.hugin_calib_create()
lib.hugin_calib_set_images(h, 3840, 2160, 3, 151.013868, 4.85, 4)
lib.hugin_calib_load_cp_file(h, b"cp.txt")
lib.hugin_calib_set_flags(h, 1 | 2 | 4)   # AUTO | FIX_LENS | DISTRIBUTE
ret = lib.hugin_calib_run(h)
print("ret =", ret)
lib.hugin_calib_destroy(h)
```

完整示例见 `examples/example_python.py`。

---

## 5. 命令行工具 (hugin_calib_driver)

动态库附带命令行工具，方便快速验证：

```bash
./bin/hugin_calib_driver \
    --width 3840 --height 2160 --proj 3 \
    --hfov 151.013868 --crop 4.85 \
    --images 4 --cp cp.txt \
    --out result.pto \
    --auto --fix-lens --distribute
```

| 选项 | 说明 |
|---|---|
| `--width W` | 图像宽度 (px, 默认 3840) |
| `--height H` | 图像高度 (px, 默认 2160) |
| `--proj P` | 投影 f-code (默认 3 = 全帧鱼眼) |
| `--hfov V` | 初始 HFOV (度, 默认 150) |
| `--crop C` | crop factor (默认 1.0) |
| `--images N` | 相机数量 (默认 2) |
| `--cp FILE` | 控制点文件 |
| `--opt LIST` | 优化变量 (默认 图0: v,a,b,c; 其他: y,p,r) |
| `--init FILE` | 初始姿态文件 (每行 yaw pitch roll) |
| `--auto` | 增量逐对预优化 |
| `--fix-lens` | 固定镜头参数 |
| `--distribute` | 自动分布初始 yaw |
| `--out FILE` | 输出 pto (默认 calib_result.pto) |

---

## 6. 控制点文件格式

每行一个对应点：

```
img1 x1 y1 img2 x2 y2
```

- `img1`, `img2`: 0-based 相机编号
- `x1, y1`: 在 img1 中的像素坐标
- `x2, y2`: 在 img2 中的像素坐标
- `#` 开头的行被忽略

示例：

```
0 3038.158936 1024.557495 1 316.285950 997.580383
0 3117.421387 1021.515503 1 388.280273 992.575989
1 3012.366243 327.375141 2 568.734213 304.067784
```

---

## 7. HFOV 计算

Hugin 用 HFOV (度) 而非焦距 (mm) 作为镜头参数。换算公式：

```
FULL_FRAME_FISHEYE:
  sensor_diag = sqrt(36² + 24²) / crop_factor
  sensor_w = sensor_diag / sqrt(1 + 1/r²)    (r = image_w / image_h)
  HFOV = sensor_w / focal_length × 180 / π
```

示例 (焦距 2.95mm, crop 4.85, 3840×2160)：

```
sensor_diag = 43.267 / 4.85 = 8.921 mm
sensor_w = 8.921 / sqrt(1 + 1/(16/9)²) = 7.775 mm
HFOV = 7.775 / 2.95 × 180 / π = 151.013868°
```

也可直接调用 `hugin_calib_calc_hfov(2.95, 4.85, 3840, 2160)`。

---

## 8. 输出 pto 格式 (AVS 兼容)

```
#hugin_optimizeReferenceImage 0
i w3840 h2160 f3 v151.014 a0 b0 c0 y-169.891 p0       r0        n"camera_0.jpg"
i w3840 h2160 f3 v151.014 a0 b0 c0 y-62.12   p-0.999663 r-0.383219 n"camera_1.jpg"
i w3840 h2160 f3 v151.014 a0 b0 c0 y44.9722  p0.849943  r3.53175  n"camera_2.jpg"
i w3840 h2160 f3 v151.014 a0 b0 c0 y163.697  p-3.36976  r-0.04098  n"camera_3.jpg"
```

- `v/a/b/c`: 镜头参数（固定，所有相机共享）
- `y/p/r`: 每路相机的姿态（优化结果，相对锚点 cam0）
- `f3`: 全帧鱼眼投影
- `w/h`: 图像尺寸

---

## 9. 4 路相机标定流程

### 9.1 拍摄方案

固定支架，分别单独拍摄两两相邻的棋盘格图片：

```
4 路相机 → 3 对相邻 (cam0↔cam1, cam1↔cam2, cam2↔cam3)
每对拍一张棋盘格照片，棋盘放在两路重叠区域
```

> **不需要同一时刻 4 路同时拍**。Hugin 优化器只用控制点坐标，不读图片像素。
> 只要支架固定，相机间相对位置不变，分对拍完全可以。

### 9.2 棋盘格规格

- 内角点: 7×4 (列×行)
- 棋盘要放在两路相机的重叠区域
- 尽量让棋盘占满重叠区，角点分布均匀

### 9.3 角点检测

使用 OpenCV `findChessboardCornersSB`（鲁棒性高）检测角点，生成控制点文件。

### 9.4 标定

```c
// C API
hugin_calib_set_images(h, 3840, 2160, 3, 151.013868, 4.85, 4);
hugin_calib_load_cp_file(h, "cp_4cam.txt");
hugin_calib_set_flags(h, HUGIN_CALIB_FLAG_AUTO |
                         HUGIN_CALIB_FLAG_FIX_LENS |
                         HUGIN_CALIB_FLAG_DISTRIBUTE);
hugin_calib_run(h);
hugin_calib_write_pto(h, "result_4cam.pto");
```

---

## 10. 与 Hugin GUI 对齐验证

通过源码分析，发现 driver 与 Hugin GUI 之间存在 **3 个关键差异**，
全部修复后，标定结果与 Hugin GUI 完全一致（误差 < 0.001°）：

| 项目 | Hugin GUI "位置（增量）" | 本库 |
|---|---|---|
| **优化变量** | 只优化 `y/p/r`（姿态） | `FIX_LENS` 标志 |
| **初始 yaw** | DistributeImagesCmd 自动分布 | `DISTRIBUTE` 标志 |
| **优化流程** | AutoOptimise 增量 + 全局 | `AUTO` 标志 |

**验证结果**（3 路相机, 15 控制点, 4K 全帧鱼眼）：

| | 官方 Hugin GUI pto | 本库 | 误差 |
|---|---|---|---|
| cam0 | y=-113.260401 p=0 r=0 | y=-113.260401 p=0 r=0 | < 0.001° |
| cam1 | y=-10.579829 p=-1.197485 r=-0.403391 | y=-10.579829 p=-1.197485 r=-0.403391 | < 0.001° |
| cam2 | y=92.712462 p=1.147191 r=-1.437652 | y=92.712462 p=1.147190 r=-1.437651 | < 0.001° |
| 镜头 | v=151.014 a=0 b=0 c=0 | v=151.014 a=0 b=0 c=0 | 完全一致 |

详细分析见 `HUGIN_GUI_ALIGNMENT.md`。

---

## 11. 从源码构建

```bash
# 依赖 (仅构建时需要, 运行时已打包):
#   libpano13-dev, lcms2, VIGRA (源码编译到 /opt/vigra)
#   aarch64 上 VIGRA 无 apt 包, 需源码编译 (见 HUGIN_GUI_ALIGNMENT.md)

cd hugin_min
mkdir -p build && cd build
cmake .. -DVIGRA_ROOT=/opt/vigra
make -j2

# 打包自包含发布目录
cd ..
./package.sh build dist
```

---

## 12. 常见问题

### Q: 目标机器报 `libpano13.so.3: cannot open shared object file`

A: 忘记设置 `LD_LIBRARY_PATH`。运行前执行：
```bash
export LD_LIBRARY_PATH=$PWD/lib
```

### Q: 可以静态链接吗？

A: 可以。`libhugin_calib.so` 是动态库，但内部已包含 huginbase 静态库的全部代码。
如果你的程序需要完全静态，可自行将 `libhugin_calib.so` 改为静态库
（CMake 中把 `SHARED` 改为 `STATIC`），但 libpano13 等依赖仍需动态。

### Q: 优化结果 RMS 误差很大？

A: 检查：
1. 控制点是否对应正确（两图角点一一对应）
2. 初始 HFOV 是否合理（用 `hugin_calib_calc_hfov` 计算）
3. 棋盘格是否占满重叠区、角点分布是否均匀
4. 是否使用了 `AUTO | FIX_LENS | DISTRIBUTE` 组合

### Q: 支持多少路相机？

A: 无上限（>= 2）。控制点文件里引用任意两路即可，未引用的相机保持初始姿态。

---

## 13. 版本

- **libhugin_calib**: 1.0.0
- 基于 Hugin 源码 (hugin_min 裁剪版)
- 平台: aarch64 (RK3588) / Linux, 也可在 x86_64 上重新编译

---

# 附录: hugin_min 原始工程说明

## 依赖（比完整 Hugin 少很多）

| 依赖 | 来源 | 说明 |
|---|---|---|
| **libpano13 ≥ 2.9.19** | `apt install libpano13-dev` | 优化器/投影数学核心，纯 C |
| **VIGRA ≥ 1.9** | 源码编译到 `/opt/vigra`（本仓库已给最小配置） | 只用了 Size2D/Diff2D 等 + impex 符号 |
| **liblcms2** | `apt install liblcms2-dev` | utils 里的 ICC 函数 |
| **Boost / wxWidgets / OpenEXR / TIFF / JPEG / FFTW / OpenGL** | **不需要** | 全部砍掉 |

> aarch64 的 Ubuntu 仓库**没有** VIGRA 包，必须源码编译（见下）。x86_64 有 `libvigraimpex-dev`。

## 编译步骤

```bash
# 1. 系统依赖
apt install -y libpano13-dev liblcms2-dev

# 2. VIGRA（aarch64 无包，源码编最小版，全部 codec 关闭）
git clone --depth 1 --branch Version-1-11-2 https://github.com/ukoethe/vigra.git /root/vigra
#   需要先修一个 CMake bug：CMakeLists.txt 里
#   ADD_DEPENDENCIES(PACKAGE_SRC_TAR doc_cpp)  两行包进 IF(BUILD_DOCS) 否则 configure 失败
cd /root/vigra && mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/opt/vigra \
  -DWITH_OPENEXR=OFF -DWITH_HDF5=OFF -DWITH_BOOST_GRAPH=OFF \
  -DWITH_VIGRANUMPY=OFF -DWITH_FFTW=OFF -DWITH_JPEG=OFF \
  -DWITH_PNG=OFF -DWITH_TIFF=OFF -DWITH_ZLIB=OFF \
  -DBUILD_TESTS=OFF -DBUILD_DOCS=OFF ..
make -j4 && make install

# 3. 编译 hugin_min
cd /root/hugin_min
cmake -S . -B build
cmake --build build -j4
# 产物: build/hugin_calib_driver
```

## 最小工程做了哪些裁剪（相对完整 Hugin）

**源码文件（~22 个 + levmar）**：`panodata/`（Panorama/PTScriptParsing/SrcPanoImage/Lens/
ControlPoint/Mask/ImageVariableGroup/StandardImageVariableGroups/ParseExp/Options/Variable）、
`panotools/`（PanoToolsInterface + PanoToolsOptimizerWrapper）、`algorithms/basic/CalculateOverlap`、
`hugin_math/`、`hugin_utils/`、`foreign/levmar/`、`src/stubs.cpp`（曝光分层 stub）。

**砍掉**：`hugin1/`(GUI+wx)、`hugin_cpfind/`、`nona/`、`photometric/`、`lensdb/`(SQLite)、
`Exiv2Helper`(EXIV2)、`ImageTransformsGPU/PanoToolsTransformGPU`(OpenGL)、`LayerStacks.cpp`(仅 stub 其
`getExposureLayers`)、Boost、OpenEXR、FFTW。

**打补丁的文件（副本，原版未动）**：
- `panodata/SrcPanoImage.cpp`：去掉 exiv2/lensdb include；`readEXIF` 返回 false；
  `checkImageSizeKnown` 不再读图；镜头数据库方法 `#if 0`；`getExifDateTime` 返回 -1。
- `hugin_utils/utils.cpp`：GPU/GL 代码（`CreateContext`/`initGPU`/`wrapupGPU`）`#if 0`，
  去掉 GL/EGL/X11 include（保留 lcms2）。

## 关键 API 用法（驱动里已有完整示例）

1. `SrcPanoImage` 设投影/尺寸/HFOV/畸变初值 → `pano.addImage()`
2. `StandardImageVariableGroups::getLenses()` 先 `switchParts(i,0)` 再 `linkVariablePart(v,0)` 共享镜头
3. `pano.addCtrlPoint(ControlPoint(i,x1,y1,j,x2,y2,ControlPoint::X_Y))`
4. `pano.setOptimizeVector(OptimizeVector)`（`vector<set<string>>`，参考图只放镜头变量）
5. `PTools::optimize(pano)` 返回 0/1/2
6. `pano.getImage(i).getVar("v"/"a"/"b"/"c"/"y"/"p"/"r")` 读回结果写 pto

## 注意事项 / 坑

- **参考图(image 0)姿态固定**（全局坐标系锚点），optvec 里别放 y/p/r。
- **fov↔姿态尺度耦合**：纯 2D 控制点标定存在 fov 与相对姿态的整体缩放歧义，
  真实棋盘（已知物理尺寸/多姿态）约束更强；初值 `v` 要给准。
- **a/b/c 初值给 0** 时 `printPanoramaScript` 会写成 `a1e-5`（libpano13 防除零），正常。
- **f3 全帧鱼眼**：Hugin 用 v+a/b/c 拟合偏离理想鱼眼投影的偏差，正是 AVS 的模型约定，
  优化出的值可直接喂 AVS。
- 无头板卡：不链接任何 GL，标定全 CPU，运行不需要显示。
