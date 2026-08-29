/*
 * hugin_calib.h — Hugin 最小化标定引擎 C API
 *
 * 基于 hugin_min (Hugin 源码裁剪) 的共享镜头标定动态库。
 * 吃控制点文件/内存数组, 输出 AVS 兼容的 pto 几何参数 (v/a/b/c + 每图 y/p/r)。
 *
 * 设计目标:
 *   - 纯 C 接口, 可被 C / C++ / Python(ctypes) / Rust(FFI) 等任意语言调用
 *   - 所有内存由库管理, 调用方只需 malloc/free 简单结构
 *   - 线程安全: 每个 handle 独立, 不共享全局状态
 *
 * 编译:
 *   gcc myapp.c -I<prefix>/include -L<prefix>/lib -lhugin_calib -o myapp
 *
 * 快速上手 (C):
 *   hugin_calib_handle h = hugin_calib_create();
 *   hugin_calib_set_images(h, 3840, 2160, HUGIN_CALIB_PROJ_FULL_FRAME_FISHEYE, 151.013868, 4.85, 4);
 *   hugin_calib_add_cp(h, 0, 3038.16, 1024.56, 1, 316.29, 997.58);
 *   ...
 *   hugin_calib_set_flags(h, HUGIN_CALIB_FLAG_AUTO | HUGIN_CALIB_FLAG_FIX_LENS | HUGIN_CALIB_FLAG_DISTRIBUTE);
 *   int ret = hugin_calib_run(h);
 *   hugin_calib_get_result(h, &result);   // 读回 v/a/b/c + 每图 y/p/r
 *   hugin_calib_write_pto(h, "result.pto");
 *   hugin_calib_destroy(h);
 */

#ifndef HUGIN_CALIB_H
#define HUGIN_CALIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* 导出宏 (Windows DLL / Linux so 通用)                                */
/* ------------------------------------------------------------------ */
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(HUGIN_CALIB_BUILDING_DLL)
#    define HUGIN_CALIB_API __declspec(dllexport)
#  elif defined(HUGIN_CALIB_USING_DLL)
#    define HUGIN_CALIB_API __declspec(dllimport)
#  else
#    define HUGIN_CALIB_API
#  endif
#else
#  define HUGIN_CALIB_API __attribute__((visibility("default")))
#endif

/* ------------------------------------------------------------------ */
/* 常量                                                                */
/* ------------------------------------------------------------------ */

/** 投影类型 (与 Hugin f-code 一致) */
enum hugin_calib_projection {
    HUGIN_CALIB_PROJ_RECTILINEAR          = 0,  /**< 直线投影 (普通镜头) */
    HUGIN_CALIB_PROJ_PANORAMIC            = 1,  /**< 全景柱状 */
    HUGIN_CALIB_PROJ_CIRCULAR_FISHEYE     = 2,  /**< 圆形鱼眼 */
    HUGIN_CALIB_PROJ_FULL_FRAME_FISHEYE   = 3,  /**< 全帧鱼眼 (4K 相机常用) */
    HUGIN_CALIB_PROJ_EQUIRECTANGULAR      = 4,  /**< 等距柱状 */
    HUGIN_CALIB_PROJ_FISHEYE_ORTHOGRAPHIC = 8,  /**< 正射鱼眼 */
    HUGIN_CALIB_PROJ_FISHEYE_STEREOGRAPHIC= 10, /**< 立体鱼眼 */
    HUGIN_CALIB_PROJ_FISHEYE_THOBY        = 20, /**< Thoby 鱼眼 */
    HUGIN_CALIB_PROJ_FISHEYE_EQUISOLID    = 21  /**< 等立体角鱼眼 */
};

/** 优化行为标志 (可组合) */
enum hugin_calib_flags {
    HUGIN_CALIB_FLAG_NONE       = 0,
    /** 增量逐对预优化 (对应 Hugin GUI "位置（增量，从锚定点开始）") */
    HUGIN_CALIB_FLAG_AUTO       = 1 << 0,
    /** 固定镜头参数, 不优化 v/a/b/c (只优化姿态 y/p/r) */
    HUGIN_CALIB_FLAG_FIX_LENS   = 1 << 1,
    /** 自动均匀分布初始 yaw (对应 Hugin GUI DistributeImagesCmd) */
    HUGIN_CALIB_FLAG_DISTRIBUTE = 1 << 2,
    /** 启用垂直线检测 (linefind): 自动检测每图竖直线并加为 t1 控制点,
     *  约束 roll/pitch。需先调用 hugin_calib_enable_linefind 设置图片目录。 */
    HUGIN_CALIB_FLAG_LINEFIND   = 1 << 3,
    /** 智能优化 (对应 Hugin 助手 autooptimiser -a / SmartOptimise):
     *  AutoOptimise + 全局姿态 + 镜头标定, 全景 FOV>=150 时自动优化
     *  v/a/b/c/d/e (会改变 HFOV)。推荐与 LINEFIND 一起用。 */
    HUGIN_CALIB_FLAG_SMART      = 1 << 4
};

/** 返回码 */
enum hugin_calib_status {
    HUGIN_CALIB_OK              = 0,  /**< 成功 */
    HUGIN_CALIB_ERR_NULL        = -1, /**< 空指针参数 */
    HUGIN_CALIB_ERR_INVALID     = -2, /**< 参数非法 (如 images < 2) */
    HUGIN_CALIB_ERR_CP_FILE     = -3, /**< 控制点文件无法打开 */
    HUGIN_CALIB_ERR_NO_CP       = -4, /**< 没有有效控制点 */
    HUGIN_CALIB_ERR_OPTIMIZE    = -5, /**< 优化器失败 (PTools::optimize 非 0) */
    HUGIN_CALIB_ERR_WRITE       = -6, /**< 输出文件写入失败 */
    HUGIN_CALIB_ERR_INIT_FILE   = -7, /**< 初始姿态文件无法打开 */
    HUGIN_CALIB_ERR_NOT_RUN     = -8  /**< 尚未运行优化, 无法读取结果 */
};

/* ------------------------------------------------------------------ */
/* 数据结构                                                            */
/* ------------------------------------------------------------------ */

/** 不透明句柄 (指向库内部状态) */
typedef struct hugin_calib_handle_t hugin_calib_handle;

/** 单张图像的标定结果 (几何参数) */
typedef struct hugin_calib_image_result {
    int    width;      /**< 图像宽度 (px) */
    int    height;     /**< 图像高度 (px) */
    int    projection; /**< 投影类型 (hugin_calib_projection) */
    double v;          /**< HFOV (度) */
    double a;          /**< 径向畸变 a */
    double b;          /**< 径向畸变 b */
    double c;          /**< 径向畸变 c */
    double d;          /**< 主点水平偏移 (px) */
    double e;          /**< 主点垂直偏移 (px) */
    double yaw;        /**< 偏航角 (度) */
    double pitch;      /**< 俯仰角 (度) */
    double roll;       /**< 翻滚角 (度) */
    char   filename[256]; /**< 图像文件名 */
} hugin_calib_image_result;

/** 整体标定结果 */
typedef struct hugin_calib_result {
    int    num_images;              /**< 图像数量 */
    double rms_error;               /**< 优化后 RMS 重投影误差 (px) */
    hugin_calib_image_result *images; /**< 每张图像的结果数组 (库分配, 调用方 free) */
} hugin_calib_result;

/* ------------------------------------------------------------------ */
/* 生命周期                                                            */
/* ------------------------------------------------------------------ */

/** 创建标定句柄。
 *  @return 新句柄, 失败返回 NULL
 */
HUGIN_CALIB_API hugin_calib_handle *hugin_calib_create(void);

/** 销毁句柄, 释放所有内部资源。h 为 NULL 时安全无操作。 */
HUGIN_CALIB_API void hugin_calib_destroy(hugin_calib_handle *h);

/* ------------------------------------------------------------------ */
/* 配置                                                                */
/* ------------------------------------------------------------------ */

/** 设置相机/图像参数 (所有图像共享同一镜头)。
 *  @param h        句柄
 *  @param width    图像宽度 (px)
 *  @param height   图像高度 (px)
 *  @param proj     投影类型 (hugin_calib_projection)
 *  @param hfov     初始 HFOV (度)
 *  @param crop     crop factor (仅元数据, 不影响优化)
 *  @param num_images 相机数量 (>= 2)
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_set_images(hugin_calib_handle *h,
                                           int width, int height,
                                           int proj, double hfov, double crop,
                                           int num_images);

/** 设置第 idx 张图像的初始姿态 (度)。
 *  默认全部为 0。使用 --distribute 时此设置会被覆盖。
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_set_initial_pose(hugin_calib_handle *h,
                                                 int idx,
                                                 double yaw, double pitch, double roll);

/** 从文件加载初始姿态, 每行 "yaw pitch roll" (度), 一行一张图。
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_load_initial_poses(hugin_calib_handle *h,
                                                   const char *filename);

/** 设置优化行为标志 (hugin_calib_flags 组合)。
 *  推荐组合: AUTO | FIX_LENS | DISTRIBUTE 完全复现 Hugin GUI "位置（增量）"。
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_set_flags(hugin_calib_handle *h, int flags);

/** 设置第 idx 张图像的优化变量 (逗号分隔, 如 "y,p,r" 或 "v,a,b,c")。
 *  默认: 图 0 优化 v,a,b,c (除非 FIX_LENS), 其他图优化 y,p,r。
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_set_optimize_vars(hugin_calib_handle *h,
                                                  int idx, const char *vars);

/* ------------------------------------------------------------------ */
/* 控制点                                                              */
/* ------------------------------------------------------------------ */

/** 添加一个控制点 (两图对应点)。
 *  @param h    句柄
 *  @param img1 图像 1 编号 (0-based)
 *  @param x1   图像 1 中的 x 坐标 (px)
 *  @param y1   图像 1 中的 y 坐标 (px)
 *  @param img2 图像 2 编号 (0-based)
 *  @param x2   图像 2 中的 x 坐标 (px)
 *  @param y2   图像 2 中的 y 坐标 (px)
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_add_cp(hugin_calib_handle *h,
                                       int img1, double x1, double y1,
                                       int img2, double x2, double y2);

/** 添加一个单图内垂直线控制点 (t1, 约束 roll/pitch)。
 *  也可由 hugin_calib_enable_linefind 自动生成。
 *  @param img 图像编号 (0-based)
 *  @param x1,y1 线起点, x2,y2 线终点 (px)
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_add_cp_line(hugin_calib_handle *h,
                                            int img, double x1, double y1,
                                            double x2, double y2);

/** 启用垂直线检测 (配合 HUGIN_CALIB_FLAG_LINEFIND 使用)。
 *  在 run() 时对每张图像检测竖直线 (门框/柱子/建筑边缘等), 自动添加为 t1 控制点。
 *  @param imgdir 图片所在目录 (包含 camera_0.png/.jpg 等), 可为 NULL 表示当前目录
 *  @param lines  每图最多检测线条数 (>0, 建议 5)
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_enable_linefind(hugin_calib_handle *h,
                                                const char *imgdir, int lines);

/** 从文件加载控制点, 每行 "img1 x1 y1 img2 x2 y2", '#' 开头行忽略。
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_load_cp_file(hugin_calib_handle *h,
                                             const char *filename);

/** 返回已添加的控制点数量。 */
HUGIN_CALIB_API int hugin_calib_get_num_cp(const hugin_calib_handle *h);

/* ------------------------------------------------------------------ */
/* 执行与结果                                                          */
/* ------------------------------------------------------------------ */

/** 运行标定优化。
 *  @return hugin_calib_status (HUGIN_CALIB_OK 表示成功)
 */
HUGIN_CALIB_API int hugin_calib_run(hugin_calib_handle *h);

/** 获取标定结果。
 *  @param result 输出结构。images 数组由库分配, 调用方用 free() 释放。
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_get_result(const hugin_calib_handle *h,
                                           hugin_calib_result *result);

/** 将结果写入 AVS 兼容的 pto 文件。
 *  @return hugin_calib_status
 */
HUGIN_CALIB_API int hugin_calib_write_pto(const hugin_calib_handle *h,
                                          const char *filename);

/** 获取最后一条错误消息 (线程局部, 只读)。 */
HUGIN_CALIB_API const char *hugin_calib_last_error(const hugin_calib_handle *h);

/* ------------------------------------------------------------------ */
/* 工具函数                                                            */
/* ------------------------------------------------------------------ */

/** 计算全帧鱼眼 HFOV (度)。
 *  @param focal_mm    焦距 (mm)
 *  @param crop_factor crop factor
 *  @param width       图像宽度 (px)
 *  @param height      图像高度 (px)
 *  @return HFOV (度)
 */
HUGIN_CALIB_API double hugin_calib_calc_hfov(double focal_mm, double crop_factor,
                                             int width, int height);

/** 返回库版本字符串。 */
HUGIN_CALIB_API const char *hugin_calib_version(void);

#ifdef __cplusplus
}
#endif

#endif /* HUGIN_CALIB_H */