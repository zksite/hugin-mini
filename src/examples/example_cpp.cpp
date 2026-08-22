// example_cpp.cpp — libhugin_calib 动态库 C++ 示例
//
// 演示:
//   - 通过 API 逐点添加控制点 (不依赖文件)
//   - 手动设置初始姿态
//   - 自定义优化变量
//   - 读取结果
//
// 编译:
//   g++ example_cpp.cpp -I<prefix>/include -L<prefix>/lib -lhugin_calib -o example_cpp
// 运行:
//   LD_LIBRARY_PATH=<prefix>/lib ./example_cpp

#include <cstdio>
#include <cstdlib>
#include "hugin_calib.h"

int main()
{
    hugin_calib_handle *h = hugin_calib_create();
    if (!h) { std::fprintf(stderr, "create failed\n"); return 1; }

    // 3 路相机, 全帧鱼眼, HFOV 151.013868 (焦距 2.95mm, crop 4.85, 4K)
    int ret = hugin_calib_set_images(h, 3840, 2160,
                                     HUGIN_CALIB_PROJ_FULL_FRAME_FISHEYE,
                                     151.013868, 4.85, 3);
    if (ret != HUGIN_CALIB_OK) {
        std::fprintf(stderr, "set_images failed: %d (%s)\n", ret, hugin_calib_last_error(h));
        hugin_calib_destroy(h);
        return 1;
    }

    // 手动设置初始姿态 (度)
    hugin_calib_set_initial_pose(h, 0, -113.26, 0.0, 0.0);
    hugin_calib_set_initial_pose(h, 1, -10.58, 0.0, 0.0);
    hugin_calib_set_initial_pose(h, 2, 92.71, 0.0, 0.0);

    // 逐点添加控制点 (cam0 <-> cam1)
    hugin_calib_add_cp(h, 0, 3038.159, 1024.557, 1, 316.286, 997.580);
    hugin_calib_add_cp(h, 0, 3117.421, 1021.516, 1, 388.280, 992.576);
    hugin_calib_add_cp(h, 0, 3201.000, 1018.000, 1, 460.000, 988.000);
    hugin_calib_add_cp(h, 0, 3280.000, 1015.000, 1, 530.000, 984.000);
    hugin_calib_add_cp(h, 0, 3360.000, 1012.000, 1, 600.000, 980.000);
    // cam1 <-> cam2
    hugin_calib_add_cp(h, 1, 3012.366, 327.375, 2, 568.734, 304.068);
    hugin_calib_add_cp(h, 1, 3100.000, 320.000, 2, 650.000, 300.000);
    hugin_calib_add_cp(h, 1, 3180.000, 315.000, 2, 720.000, 295.000);
    hugin_calib_add_cp(h, 1, 3260.000, 310.000, 2, 790.000, 290.000);

    std::printf("added %d control points\n", hugin_calib_get_num_cp(h));

    // 自定义优化变量: 图 0 只优化 v, 其他图优化 y/p/r
    hugin_calib_set_optimize_vars(h, 0, "v");
    hugin_calib_set_optimize_vars(h, 1, "y,p,r");
    hugin_calib_set_optimize_vars(h, 2, "y,p,r");

    // 运行 (增量逐对 + 全局优化, 固定镜头)
    ret = hugin_calib_set_flags(h, HUGIN_CALIB_FLAG_AUTO | HUGIN_CALIB_FLAG_FIX_LENS);
    if (ret != HUGIN_CALIB_OK) { std::fprintf(stderr, "set_flags failed\n"); hugin_calib_destroy(h); return 1; }

    ret = hugin_calib_run(h);
    if (ret != HUGIN_CALIB_OK) {
        std::fprintf(stderr, "run failed: %d (%s)\n", ret, hugin_calib_last_error(h));
        hugin_calib_destroy(h);
        return 1;
    }

    hugin_calib_result result;
    if (hugin_calib_get_result(h, &result) != HUGIN_CALIB_OK) {
        std::fprintf(stderr, "get_result failed\n");
        hugin_calib_destroy(h);
        return 1;
    }

    std::printf("RMS error: %.4f px\n", result.rms_error);
    for (int i = 0; i < result.num_images; ++i) {
        const hugin_calib_image_result *im = &result.images[i];
        std::printf("cam%d: v=%.6f a=%.6f b=%.6f c=%.6f y=%.6f p=%.6f r=%.6f\n",
                    i, im->v, im->a, im->b, im->c, im->yaw, im->pitch, im->roll);
    }
    std::free(result.images);

    hugin_calib_write_pto(h, "result_cpp.pto");
    std::printf("wrote result_cpp.pto\n");

    hugin_calib_destroy(h);
    return 0;
}