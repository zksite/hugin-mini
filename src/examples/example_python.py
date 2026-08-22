#!/usr/bin/env python3
"""
example_python.py — 用 Python ctypes 调用 libhugin_calib 动态库

演示:
  - 加载动态库
  - 设置相机参数 (3 路全帧鱼眼 4K)
  - 加载控制点文件
  - 设置优化标志 (复现 Hugin GUI "位置（增量）")
  - 运行优化并读取结果

用法:
  LD_LIBRARY_PATH=dist/lib python3 example_python.py dist/lib/libhugin_calib.so cp.txt
"""

import ctypes
import sys
import os

def main():
    lib_path = sys.argv[1] if len(sys.argv) > 1 else "dist/lib/libhugin_calib.so"
    cp_file = sys.argv[2] if len(sys.argv) > 2 else "examples/cp_sample.txt"

    lib = ctypes.CDLL(os.path.abspath(lib_path))

    # ---- 声明函数签名 ----
    lib.hugin_calib_create.restype = ctypes.c_void_p
    lib.hugin_calib_destroy.argtypes = [ctypes.c_void_p]

    lib.hugin_calib_set_images.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ctypes.c_double, ctypes.c_double, ctypes.c_int]
    lib.hugin_calib_set_images.restype = ctypes.c_int

    lib.hugin_calib_load_cp_file.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.hugin_calib_load_cp_file.restype = ctypes.c_int

    lib.hugin_calib_set_flags.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.hugin_calib_set_flags.restype = ctypes.c_int

    lib.hugin_calib_run.argtypes = [ctypes.c_void_p]
    lib.hugin_calib_run.restype = ctypes.c_int

    lib.hugin_calib_get_num_cp.argtypes = [ctypes.c_void_p]
    lib.hugin_calib_get_num_cp.restype = ctypes.c_int

    lib.hugin_calib_last_error.argtypes = [ctypes.c_void_p]
    lib.hugin_calib_last_error.restype = ctypes.c_char_p

    lib.hugin_calib_calc_hfov.argtypes = [ctypes.c_double, ctypes.c_double,
                                          ctypes.c_int, ctypes.c_int]
    lib.hugin_calib_calc_hfov.restype = ctypes.c_double

    # ---- 结果结构体 ----
    class ImageResult(ctypes.Structure):
        _fields_ = [
            ("width", ctypes.c_int),
            ("height", ctypes.c_int),
            ("projection", ctypes.c_int),
            ("v", ctypes.c_double),
            ("a", ctypes.c_double),
            ("b", ctypes.c_double),
            ("c", ctypes.c_double),
            ("yaw", ctypes.c_double),
            ("pitch", ctypes.c_double),
            ("roll", ctypes.c_double),
            ("filename", ctypes.c_char * 256),
        ]

    class Result(ctypes.Structure):
        _fields_ = [
            ("num_images", ctypes.c_int),
            ("rms_error", ctypes.c_double),
            ("images", ctypes.POINTER(ImageResult)),
        ]

    lib.hugin_calib_get_result.argtypes = [ctypes.c_void_p, ctypes.POINTER(Result)]
    lib.hugin_calib_get_result.restype = ctypes.c_int

    lib.hugin_calib_write_pto.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.hugin_calib_write_pto.restype = ctypes.c_int

    # ---- 使用 ----
    h = lib.hugin_calib_create()
    if not h:
        print("create failed")
        return 1

    hfov = lib.hugin_calib_calc_hfov(2.95, 4.85, 3840, 2160)
    print(f"HFOV = {hfov:.6f} deg")

    ret = lib.hugin_calib_set_images(h, 3840, 2160, 3, hfov, 4.85, 3)
    if ret != 0:
        print(f"set_images failed: {ret} {lib.hugin_calib_last_error(h)}")
        return 1

    ret = lib.hugin_calib_load_cp_file(h, cp_file.encode())
    if ret != 0:
        print(f"load_cp_file failed: {ret} {lib.hugin_calib_last_error(h)}")
        return 1
    print(f"loaded {lib.hugin_calib_get_num_cp(h)} control points")

    # 复现 Hugin GUI "位置（增量，从锚定点开始）"
    FLAG_AUTO = 1
    FLAG_FIX_LENS = 2
    FLAG_DISTRIBUTE = 4
    ret = lib.hugin_calib_set_flags(h, FLAG_AUTO | FLAG_FIX_LENS | FLAG_DISTRIBUTE)
    if ret != 0:
        print(f"set_flags failed: {ret}")
        return 1

    ret = lib.hugin_calib_run(h)
    if ret != 0:
        print(f"run failed: {ret} {lib.hugin_calib_last_error(h)}")
        return 1

    result = Result()
    ret = lib.hugin_calib_get_result(h, ctypes.byref(result))
    if ret != 0:
        print(f"get_result failed: {ret}")
        return 1

    print(f"RMS error: {result.rms_error:.4f} px")
    for i in range(result.num_images):
        im = result.images[i]
        print(f"cam{i}: v={im.v:.6f} a={im.a:.6f} b={im.b:.6f} c={im.c:.6f} "
              f"y={im.yaw:.6f} p={im.pitch:.6f} r={im.roll:.6f}")

    ctypes.CDLL(None).free(result.images)  # 释放库分配的内存

    lib.hugin_calib_write_pto(h, b"result_python.pto")
    print("wrote result_python.pto")

    lib.hugin_calib_destroy(h)
    return 0

if __name__ == "__main__":
    sys.exit(main())