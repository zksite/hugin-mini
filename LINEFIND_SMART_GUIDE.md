# linefind（垂直线检测）与 Smart 优化 使用指南

**版本**: 1.2.0 | **日期**: 2026-08-29

本文档说明 hugin_min 新增的两项能力：

1. **linefind 垂直线检测** — 自动检测每张图像中的竖直线（门框/柱子/建筑边缘），
   生成 `t1` 垂直线控制点，约束相机的 **roll/pitch**（横滚/俯仰）。
2. **Smart 智能优化** — 复现 Hugin 助手 "Align"（`autooptimiser -a`）流程，
   **自动优化 HFOV (v) 和镜头畸变 (a/b/c/d/e)**，全景 FOV≥150° 时 v 会被重新标定。

两者结合，可以在「棋盘控制点 + 背景竖直线」的场景下，得到与 Hugin GUI 助手一致的标定结果。

---

## 1. 为什么需要这两项

| 功能 | 解决的问题 |
|---|---|
| **棋盘 t0 控制点**（跨图） | 约束相机间相对姿态（yaw/pitch/roll）、HFOV、镜头畸变 |
| **垂直线 t1 控制点**（单图内） | 约束相机**相对垂直方向**的 roll/pitch，校正相机歪斜 |
| **Smart 优化** | 自动重标定 HFOV (v) 与畸变（官方助手会这么做，v 从 151→133 就是它优化的） |

**典型场景**：棋盘控制点标定相机阵列的旋转/畸变，同时在背景放置竖直线条
（用铅锤/水平仪保证真实垂直），自动检测垂直线校正各相机的 roll/pitch。

---

## 2. 命令行用法（hugin_calib_driver）

```bash
./hugin_calib_driver \
    --width 3840 --height 2160 --proj 3 \
    --hfov 151.013868 --crop 4.85 \        # ★ crop 必须正确（默认 1.0 会导致垂直线检测失败）
    --images 3 \
    --cp cp.txt \                          # 棋盘控制点 (img1 x1 y1 img2 x2 y2)
    --imgdir /path/to/images \             # 图片目录 (camera_0.png/.jpg ...)
    --linefind --lines 10 \                 # 自动检测垂直线，每图最多 10 条
    --smart \                              # SmartOptimise（自动标定 v/a/b/c/d/e）
    --out result.pto
```

### 参数说明

| 参数 | 含义 | 默认 |
|---|---|---|
| `--hfov V` | 初始 HFOV（度） | 150 |
| `--crop C` | **crop factor，必须与相机一致**（否则垂直线检测失败） | 1.0 |
| `--imgdir DIR` | 图片目录，自动匹配 `camera_<i>.png/.jpg/.jpeg` | `.` |
| `--linefind` | 检测每图竖直线并加为 t1 控制点 | 关 |
| `--lines N` | 每图最多检测线条数 | 10 |
| `--smart` | SmartOptimise（内部含 AutoOptimise + 镜头标定） | 关 |

### 与旧参数的配合
- `--smart` **不要**与 `--fix-lens` 同用（smart 会自己决定是否优化 v/a/b/c/d/e）
- 锚点（图 0）的 yaw 不参与优化，若需要固定全局朝向，用 `--init` 指定 cam0 的 yaw

---

## 3. C API 用法（libhugin_calib.so）

### 3.1 新增标志

```c
enum hugin_calib_flags {
    HUGIN_CALIB_FLAG_NONE       = 0,
    HUGIN_CALIB_FLAG_AUTO       = 1 << 0,  // AutoOptimise 增量预优化
    HUGIN_CALIB_FLAG_FIX_LENS   = 1 << 1,  // 固定镜头参数（不与 SMART 同用）
    HUGIN_CALIB_FLAG_DISTRIBUTE = 1 << 2,  // 均匀分布初始 yaw
    HUGIN_CALIB_FLAG_LINEFIND   = 1 << 3,  // 启用垂直线检测
    HUGIN_CALIB_FLAG_SMART      = 1 << 4   // 智能优化（自动标定 v/a/b/c/d/e）
};
```

### 3.2 新增 API

```c
/** 启用垂直线检测（配合 HUGIN_CALIB_FLAG_LINEFIND） */
int hugin_calib_enable_linefind(hugin_calib_handle *h,
                                const char *imgdir,  // 图片目录，可为 NULL
                                int lines);          // 每图最多条数（建议 10）

/** 手动添加单图内垂直线控制点（t1） */
int hugin_calib_add_cp_line(hugin_calib_handle *h,
                            int img, double x1, double y1,
                            double x2, double y2);
```

### 3.3 C 示例

```c
#include "hugin_calib.h"

int main() {
    hugin_calib_handle *h = hugin_calib_create();

    // 1. 相机参数（所有图像共享镜头）
    hugin_calib_set_images(h, 3840, 2160,
                           HUGIN_CALIB_PROJ_FULL_FRAME_FISHEYE,
                           151.013868, 4.85, 3);

    // 2. 棋盘控制点
    hugin_calib_load_cp_file(h, "cp.txt");

    // 3. 启用垂直线检测 + 智能优化
    hugin_calib_enable_linefind(h, "/path/to/images", 10);
    hugin_calib_set_flags(h, HUGIN_CALIB_FLAG_LINEFIND | HUGIN_CALIB_FLAG_SMART);

    // 4. 运行
    if (hugin_calib_run(h) != HUGIN_CALIB_OK) {
        printf("失败: %s\n", hugin_calib_last_error(h));
        return 1;
    }

    // 5. 输出
    hugin_calib_write_pto(h, "result.pto");

    hugin_calib_destroy(h);
    return 0;
}
```

### 3.4 Python (ctypes) 示例

```python
import ctypes
lib = ctypes.CDLL("libhugin_calib.so")
lib.hugin_calib_create.restype = ctypes.c_void_p
lib.hugin_calib_set_images.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                       ctypes.c_int, ctypes.c_double, ctypes.c_double, ctypes.c_int]
lib.hugin_calib_load_cp_file.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.hugin_calib_enable_linefind.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
lib.hugin_calib_set_flags.argtypes = [ctypes.c_void_p, ctypes.c_int]
lib.hugin_calib_run.argtypes = [ctypes.c_void_p]
lib.hugin_calib_write_pto.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

h = lib.hugin_calib_create()
lib.hugin_calib_set_images(h, 3840, 2160, 3, 151.013868, 4.85, 3)
lib.hugin_calib_load_cp_file(h, b"cp.txt")
lib.hugin_calib_enable_linefind(h, b"/path/to/images", 10)
lib.hugin_calib_set_flags(h, (1 << 4) | (1 << 3))   # SMART | LINEFIND
lib.hugin_calib_run(h)
lib.hugin_calib_write_pto(h, b"result.pto")
lib.hugin_calib_destroy(h)
```

---

### 3.5 读取垂线检出数量 (v1.2.0+)

`hugin_calib_image_result` 新增 `num_lines` 字段，表示**该图实际检测到的垂直线数量**
（linefind 未启用或图像处理失败时为 0）：

```c
typedef struct hugin_calib_image_result {
    ...
    double roll;        /* 翻滚角 (度) */
    int    num_lines;   /* 该图检测到的垂直线数量 (linefind 启用时为实际检出数, 否则 0) */
    char   filename[256];
} hugin_calib_image_result;
```

- `hugin_calib_result.images[i].num_lines` 即为第 i 张图找出的垂线数
- 调用方无需额外 API，`hugin_calib_get_result` 后直接读该字段即可

⚠️ **ABI 注意**：结构体末尾新增字段后，调用方必须使用与本 .so **同版本**的头文件 /
ctypes 结构体重新编译，否则后续字段会错位（历史教训：旧头 + 新 .so → 姿态字段错位）。

---

## 4. 控制点文件格式（扩展）

`--cp` / `hugin_calib_load_cp_file` 支持 6 列或 7 列：

```
# 6 列: 跨图对应点 (t0, X_Y)
0 2719.27 204.03 1 344.32 50.52
# 7 列: 第 7 列是类型 0=X_Y, 1=垂直线, 2=水平线
2 2939.42 585.58 2 2902.34 1499.87 1
```

垂直线也可用 `hugin_calib_add_cp_line` 或 `--linefind` 自动生成。

---

## 5. 最佳实践与注意事项

### 垂直线检测（linefind）效果取决于场景
- **线条要求**：真实垂直（铅锤/水平仪校验）、高对比、清晰边缘、长度 > 20px、
  相互间距 ≥ 80px、分布在画面不同位置
- **crop factor 必须正确**：检测用焦距拟合直线，`crop` 错 → 焦距错 → 检测失败。
  这是最常见的坑（默认 1.0，4 路相机应传 4.85）
- 检测数量：默认每图上限 10 条（v1.2.0 从 5 上调），一般 5~10 条即可，不是越多越好

### Smart 优化
- 全景 FOV ≥ 150° 时才会自动优化 HFOV (v)；低于则只优化畸变 a/b/c
- 不要与 `FIX_LENS` 同用
- 锚点图（图 0）的 yaw 不优化，用 `set_initial_pose`/`--init` 指定全局朝向

### 输出
- 输出 pto 为 AVS 兼容几何格式（v/a/b/c + 每图 y/p/r）
- 文件名自动匹配实际图片（`camera_i.png` 等），不再写死 `.jpg`

---

## 6. 已知限制
- 垂直线只约束 roll/pitch，**不约束 yaw/HFOV**（仍需要棋盘 t0 控制点）
- 多机共享镜头时所有图像共用一个 v/a/b/c（标准共享镜头标定）
- 无 OpenGL 的构建中 GPU remap 被禁用（CPU remap），检测速度略慢但结果一致

---

## 7. 版本变更记录

| 版本 | 变更 |
|---|---|
| 1.1.0 | 新增 `LINEFIND`/`SMART` 标志、`enable_linefind`/`add_cp_line`、结果含 d/e 主点偏移 |
| 1.2.0 | ① 垂直线每图上限默认 5→10；② `hugin_calib_image_result` 新增 `num_lines` 字段（每图垂线检出数） |
