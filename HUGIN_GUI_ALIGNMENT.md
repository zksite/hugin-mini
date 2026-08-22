# Hugin GUI 对齐标定指南

**日期**: 2026-08-21 | **平台**: RK3588 (aarch64, Ubuntu 22.04)

---

## 1. 概述

本文档记录如何使用 `hugin_min` 标定引擎**完全复现** Hugin GUI 软件的标定结果，
以及如何用 4 路相机两两拍摄棋盘格进行标定。

### 核心结论

通过源码分析，发现 `hugin_min` driver 与 Hugin GUI 之间存在 **3 个关键差异**，
全部修复后，标定结果与 Hugin GUI 完全一致（误差 < 0.001°）。

---

## 2. Hugin GUI 标定流程分析

### 2.1 用户操作步骤

在 Hugin GUI 中执行以下操作：

1. **加载图片** — 拖入相机拍摄的图片
2. **选择镜头类型** — 全帧鱼眼 (Full-frame fisheye, f3)，输入焦距 2.95mm，焦距乘法器 4.85
3. **特征匹配** — Hugin 内置 CPFind 自动创建控制点
4. **手动添加控制点** — 在控制点编辑器中手动标注
5. **优化** — 选择"几何 → 位置（增量，从锚定点开始）"

### 2.2 GUI 内部执行流程

对应源码 `OptimizePanel.cpp::runOptimizer()`：

```
步骤1: 加载图片
  └─ DistributeImagesCmd (PanoCommand.cpp:556)
     自动均匀分布初始 yaw:
     offset = 0.75 × HFOV
     cam0.yaw = -(N-1)/2 × offset  (锚点, 不优化 yaw)
     cam_i.yaw = cam0 + i × offset

步骤2: 设置镜头参数
  └─ calcHFOV(FULL_FRAME_FISHEYE) = sensor_x / focalLength × 180/π
     焦距 2.95mm + crop 4.85 + 3840×2160 → v = 151.013868°

步骤3: 优化 (OPT_PAIR 模式, optimizerMasterSwitch=1)
  ├─ 临时改输出投影: f3 鱼眼 → f2 等距柱状 (EQUIRECTANGULAR)
  ├─ AutoOptimise::autoOptimise()
  │   └─ BFS 逐对增量优化, 只优化 r/p/y (姿态)
  └─ PTools::optimize()
      └─ 全局 LM 优化, 用 GUI 勾选的变量 (只有 r/p/y)
```

### 2.3 关键发现

| 项目 | Hugin GUI "位置（增量）" | driver 原版 |
|---|---|---|
| **优化变量** | 只优化 `y/p/r`（姿态） | 优化 `v/a/b/c` + `y/p/r` |
| **镜头参数** | 固定 v=151.014, a=b=c=0 | 一起参与优化 → v 塌缩 |
| **初始 yaw** | DistributeImagesCmd 自动分布 | 全部为 0 |
| **优化流程** | AutoOptimise 增量 + 全局 | 直接全局优化 |

---

## 3. 三个关键修复

### 修复 1: `--fix-lens`（固定镜头参数）

**问题**: Hugin GUI "位置（增量）"模式只优化姿态 `y/p/r`，不优化镜头 `v/a/b/c`。
driver 原版默认优化 v/a/b/c，导致镜头参数塌缩（v 从 151 → 128，a/b/c 大畸变）。

**修复**: 新增 `--fix-lens` 选项，不优化镜头变量，保持 v/a/b/c 固定。

### 修复 2: `--distribute`（自动分布初始 yaw）

**问题**: Hugin GUI 加载图片后自动调用 `DistributeImagesCmd` 均匀分布初始 yaw。
锚点 cam0 的 yaw = -(N-1)/2 × offset = -113.26°（锚点不优化 yaw，保持初始值）。
driver 原版全部初始 yaw=0，导致全局旋转偏移。

**DistributeImagesCmd 公式**（非 rectilinear 投影）:

```
imgsPerRow = N  (鱼眼全部一行)
offset = 0.75 × HFOV
若 (N-1) × offset > 360, 则 offset = 360 / (N-1)
cam0.yaw = -(N-1)/2 × offset
cam_i.yaw = cam0 + i × offset
```

**修复**: 新增 `--distribute` 选项，自动计算并设置初始 yaw。

### 修复 3: `--auto`（增量优化流程）

**问题**: Hugin GUI 的 OPT_PAIR 模式先做 AutoOptimise（BFS 逐对增量优化），
再做全局优化。driver 原版直接全局优化。

**修复**: 新增 `--auto` 选项，调用 `AutoOptimise::autoOptimise()` 增量预优化。

---

## 4. 完全对齐验证

### 4.1 测试数据

- 3 路相机 (cam0, cam1, cam2)，4K 全帧鱼眼
- 焦距 2.95mm，crop factor 4.85
- HFOV = 151.013868°
- 15 个控制点 (cam0↔cam1: 11, cam1↔cam2: 4)

### 4.2 命令

```bash
./hugin_calib_driver \
    --width 3840 --height 2160 --proj 3 \
    --hfov 151.013868 --crop 4.85 \
    --images 3 --cp cp_img3.txt \
    --out result.pto \
    --auto --fix-lens --distribute
```

### 4.3 对比结果

| | 官方 Hugin GUI pto | driver `--auto --fix-lens --distribute` | 误差 |
|---|---|---|---|
| cam0 | y=-113.260401 p=0 r=0 | y=-113.26 p=0 r=0 | < 0.001° |
| cam1 | y=-10.579829 p=-1.197485 r=-0.403391 | y=-10.5798 p=-1.19748 r=-0.403392 | < 0.001° |
| cam2 | y=92.712462 p=1.147191 r=-1.437652 | y=92.7125 p=1.14719 r=-1.43765 | < 0.001° |
| 镜头 | v=151.014 a=0 b=0 c=0 | v=151.014 a=0 b=0 c=0 | 完全一致 |

**结论: 所有参数完全对齐，误差仅来自输出精度（6 位 vs 12 位小数）。**

---

## 5. 4 路相机标定流程

### 5.1 拍摄方案

固定支架，分别单独拍摄两两相邻的棋盘格图片：

```
4 路相机 → 3 对相邻 (cam0↔cam1, cam1↔cam2, cam2↔cam3)
每对拍一张棋盘格照片，棋盘放在两路重叠区域
```

> **不需要同一时刻 4 路同时拍**。Hugin 优化器只用控制点坐标，不读图片像素。
> 只要支架固定，相机间相对位置不变，分对拍完全可以。

### 5.2 棋盘格规格

- 内角点: 7×4 (列×行)
- 棋盘要放在两路相机的重叠区域
- 尽量让棋盘占满重叠区，角点分布均匀

### 5.3 角点检测

使用 OpenCV `findChessboardCornersSB`（鲁棒性高）检测角点:

```bash
cd /root/calib_4cam
python3 detect_corners.py
```

输出:
- `cp_4cam.txt` — 控制点文件（格式: `img1 x1 y1 img2 x2 y2`）
- `*/cam*_corners.png` — 角点可视化图（用于检查检测质量）

**注意事项**:
- SB 检测对同一张图可能返回不同的角点起点（左上角或右下角）
- 脚本自动检测角点排列方向，必要时翻转以确保两图角点一一对应
- 某些图片原图检测失败时，自动尝试降采样、均衡化等预处理

### 5.4 标定

```bash
cd /root/hugin_min/build

./hugin_calib_driver \
    --width 3840 --height 2160 --proj 3 \
    --hfov 151.013868 --crop 4.85 \
    --images 4 --cp /root/calib_4cam/cp_4cam.txt \
    --out /root/calib_4cam/result_4cam.pto \
    --auto --fix-lens --distribute
```

### 5.5 输出 pto 格式

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

## 6. driver 命令行参数

```
Usage: hugin_calib_driver [options]
  --width  W        图像宽度 (px, 默认 3840)
  --height H        图像高度 (px, 默认 2160)
  --proj  P         投影类型 f-code (0=rect, 2=circ fisheye, 3=full-frame fisheye, ...) 默认 3
  --hfov V          初始 HFOV (度, 默认 150)
  --crop C          crop factor, 仅元数据 (默认 1.0)
  --images N        相机数量 (默认 2)
  --cp FILE         控制点文件: 每行 'img1 x1 y1 img2 x2 y2'
  --opt LIST        优化变量, 逗号分隔 (默认 v,a,b,c 给图0, y,p,r 给其他)
  --init FILE       初始姿态, 每行 'yaw pitch roll' (度, 默认全 0)
  --auto            增量逐对预优化 (对应 Hugin "位置（增量）")
  --fix-lens        固定镜头参数, 不优化 v/a/b/c
  --distribute      自动分布初始 yaw (对应 Hugin DistributeImagesCmd)
  --out FILE        输出 pto 文件名 (默认 calib_result.pto)
```

### 常用组合

| 场景 | 选项 |
|---|---|
| **完全复现 Hugin GUI "位置（增量）"** | `--auto --fix-lens --distribute` |
| 标定镜头参数 (v/a/b/c + 姿态) | `--auto` (不加 --fix-lens) |
| 只优化姿态, 镜头已知 | `--auto --fix-lens` |
| 手动指定初始姿态 | `--init pose.txt --auto --fix-lens` |

---

## 7. HFOV 计算

Hugin 用 HFOV (度) 而非焦距 (mm) 作为镜头参数。换算公式:

```
FULL_FRAME_FISHEYE:
  sensor_diag = sqrt(36² + 24²) / crop_factor
  sensor_w = sensor_diag / sqrt(1 + 1/r²)    (r = image_w / image_h)
  HFOV = sensor_w / focal_length × 180 / π
```

示例 (焦距 2.95mm, crop 4.85, 3840×2160):

```
sensor_diag = 43.267 / 4.85 = 8.921 mm
sensor_w = 8.921 / sqrt(1 + 1/(16/9)²) = 7.775 mm
HFOV = 7.775 / 2.95 × 180 / π = 151.013868°
```

---

## 8. 控制点文件格式

每行一个控制点对应:

```
img1 x1 y1 img2 x2 y2
```

- `img1`, `img2`: 0-based 相机编号
- `x1, y1`: 在 img1 中的像素坐标
- `x2, y2`: 在 img2 中的像素坐标
- `#` 开头的行被忽略

示例:

```
0 3038.158936 1024.557495 1 316.285950 997.580383
0 3117.421387 1021.515503 1 388.280273 992.575989
1 3012.366243 327.375141 2 568.734213 304.067784
```

---

## 9. 文件清单

| 文件 | 说明 |
|---|---|
| `/root/hugin_min/build/hugin_calib_driver` | 标定驱动可执行文件 |
| `/root/hugin_min/src/driver.cpp` | 驱动源码 |
| `/root/calib_4cam/detect_corners.py` | 角点检测脚本 |
| `/root/calib_4cam/cp_4cam.txt` | 4 路控制点文件 |
| `/root/calib_4cam/result_4cam.pto` | 4 路标定结果 |
| `/root/img3/camera_0 - camera_2.pto` | Hugin GUI 官方 pto (对照基准) |
| `/root/img3/result_driver_final.pto` | driver 对齐结果 (3 路) |

---

## 10. 总结

```
Hugin GUI "位置（增量，从锚定点开始)"
  = --auto        (AutoOptimise 增量逐对优化)
  + --fix-lens    (只优化 y/p/r, 固定 v/a/b/c)
  + --distribute  (DistributeImagesCmd 自动分布初始 yaw)
```

三个选项组合使用，即可完全复现 Hugin GUI 的标定结果。
