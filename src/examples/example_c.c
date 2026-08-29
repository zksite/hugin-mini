/*
 * example_c.c — libhugin_calib 动态库 C 语言示例
 *
 * 演示完整标定流程:
 *   1. 创建句柄
 *   2. 设置相机参数 (4K 全帧鱼眼, 焦距 2.95mm, crop 4.85)
 *   3. 从文件加载控制点
 *   4. 设置优化标志 (完全复现 Hugin GUI "位置（增量）")
 *   5. 运行优化
 *   6. 读取结果 + 写 pto
 *
 * 编译:
 *   gcc example_c.c -I<prefix>/include -L<prefix>/lib -lhugin_calib -o example_c
 * 运行:
 *   LD_LIBRARY_PATH=<prefix>/lib ./example_c cp.txt result.pto
 */

#include <stdio.h>
#include <stdlib.h>
#include "hugin_calib.h"

int main(int argc, char **argv)
{
    const char *cp_file = (argc > 1) ? argv[1] : "cp.txt";
    const char *out_pto = (argc > 2) ? argv[2] : "result.pto";

    /* 1. 创建句柄 */
    hugin_calib_handle *h = hugin_calib_create();
    if (!h) {
        fprintf(stderr, "create failed\n");
        return 1;
    }

    /* 2. 设置相机参数: 4K 全帧鱼眼, 焦距 2.95mm, crop 4.85, 4 路相机 */
    double hfov = hugin_calib_calc_hfov(2.95, 4.85, 3840, 2160);
    printf("computed HFOV = %.6f deg\n", hfov);

    int ret = hugin_calib_set_images(h, 3840, 2160,
                                     HUGIN_CALIB_PROJ_FULL_FRAME_FISHEYE,
                                     hfov, 4.85, 4);
    if (ret != HUGIN_CALIB_OK) {
        fprintf(stderr, "set_images failed: %d (%s)\n", ret, hugin_calib_last_error(h));
        hugin_calib_destroy(h);
        return 1;
    }

    /* 3. 加载控制点文件 (每行: img1 x1 y1 img2 x2 y2) */
    ret = hugin_calib_load_cp_file(h, cp_file);
    if (ret != HUGIN_CALIB_OK) {
        fprintf(stderr, "load_cp_file failed: %d (%s)\n", ret, hugin_calib_last_error(h));
        hugin_calib_destroy(h);
        return 1;
    }
    printf("loaded %d control points\n", hugin_calib_get_num_cp(h));

    /* 4. 设置优化标志: 完全复现 Hugin GUI "位置（增量，从锚定点开始）" */
    ret = hugin_calib_set_flags(h, HUGIN_CALIB_FLAG_AUTO |
                                   HUGIN_CALIB_FLAG_FIX_LENS |
                                   HUGIN_CALIB_FLAG_DISTRIBUTE);
    if (ret != HUGIN_CALIB_OK) {
        fprintf(stderr, "set_flags failed: %d\n", ret);
        hugin_calib_destroy(h);
        return 1;
    }

    /* 5. 运行优化 */
    ret = hugin_calib_run(h);
    if (ret != HUGIN_CALIB_OK) {
        fprintf(stderr, "run failed: %d (%s)\n", ret, hugin_calib_last_error(h));
        hugin_calib_destroy(h);
        return 1;
    }

    /* 6. 读取结果 */
    hugin_calib_result result;
    ret = hugin_calib_get_result(h, &result);
    if (ret != HUGIN_CALIB_OK) {
        fprintf(stderr, "get_result failed: %d\n", ret);
        hugin_calib_destroy(h);
        return 1;
    }

    printf("RMS error: %.4f px\n", result.rms_error);
    for (int i = 0; i < result.num_images; ++i) {
        const hugin_calib_image_result *im = &result.images[i];
        printf("cam%d: v=%.6f a=%.6f b=%.6f c=%.6f d=%.4f e=%.4f "
               "y=%.6f p=%.6f r=%.6f lines=%d\n",
               i, im->v, im->a, im->b, im->c, im->d, im->e,
               im->yaw, im->pitch, im->roll, im->num_lines);
    }
    free(result.images); /* 库分配, 调用方释放 */

    /* 7. 写 pto 文件 (AVS 兼容) */
    ret = hugin_calib_write_pto(h, out_pto);
    if (ret != HUGIN_CALIB_OK) {
        fprintf(stderr, "write_pto failed: %d\n", ret);
        hugin_calib_destroy(h);
        return 1;
    }
    printf("wrote %s\n", out_pto);

    hugin_calib_destroy(h);
    return 0;
}