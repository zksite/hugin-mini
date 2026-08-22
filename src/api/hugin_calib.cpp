// hugin_calib.cpp — Hugin 最小化标定引擎 C API 实现
//
// 将 hugin_min 的标定流程封装为纯 C 接口的动态库:
//   1. 构建 Panorama (共享镜头, 初始姿态)
//   2. 加载控制点
//   3. 设置优化变量 (默认: 图0 v/a/b/c, 其他 y/p/r)
//   4. 运行 AutoOptimise (可选) + PTools::optimize
//   5. 输出 AVS 兼容 pto / 内存结果
//
// 线程安全: 所有状态保存在 handle 内, 无全局可变状态。

#include "hugin_calib.h"

#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <sstream>
#include <iostream>

#include <panodata/Panorama.h>
#include <panodata/SrcPanoImage.h>
#include <panodata/ControlPoint.h>
#include <panodata/PanoramaOptions.h>
#include <panodata/StandardImageVariableGroups.h>
#include <panotools/PanoToolsOptimizerWrapper.h>
#include <algorithms/optimizer/PTOptimizer.h>

using namespace HuginBase;

// ------------------------------------------------------------------
// 内部状态
// ------------------------------------------------------------------

struct hugin_calib_handle_t
{
    int width = 3840;
    int height = 2160;
    int proj = BaseSrcPanoImage::FULL_FRAME_FISHEYE;
    double hfov = 150.0;
    double crop = 1.0;
    int num_images = 0;

    int flags = 0;

    // 每张图像的初始姿态 (度)
    std::vector<std::array<double, 3>> init_pose;
    // 每张图像的优化变量 (空 = 使用默认)
    std::vector<std::set<std::string>> opt_vars;

    // 控制点
    std::vector<std::array<double, 6>> cps; // img1 x1 y1 img2 x2 y2

    // 结果
    bool ran = false;
    double rms_error = 0.0;
    std::vector<hugin_calib_image_result> results;

    std::string last_error;
};

// ------------------------------------------------------------------
// 内部工具
// ------------------------------------------------------------------

static void set_error(hugin_calib_handle *h, const std::string &msg)
{
    if (h) h->last_error = msg;
}

static bool valid_handle(const hugin_calib_handle *h)
{
    return h != nullptr;
}

// 计算 RMS 重投影误差 (px)
static double compute_rms(const Panorama &pano)
{
    const CPVector &cps = pano.getCtrlPoints();
    if (cps.empty()) return 0.0;
    double sum = 0.0;
    for (const auto &cp : cps)
    {
        sum += cp.error * cp.error;
    }
    return std::sqrt(sum / (double)cps.size());
}

// ------------------------------------------------------------------
// 生命周期
// ------------------------------------------------------------------

extern "C" hugin_calib_handle *hugin_calib_create(void)
{
    hugin_calib_handle *h = new (std::nothrow) hugin_calib_handle();
    return h;
}

extern "C" void hugin_calib_destroy(hugin_calib_handle *h)
{
    delete h;
}

// ------------------------------------------------------------------
// 配置
// ------------------------------------------------------------------

extern "C" int hugin_calib_set_images(hugin_calib_handle *h,
                                      int width, int height,
                                      int proj, double hfov, double crop,
                                      int num_images)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    if (num_images < 2) { set_error(h, "num_images must be >= 2"); return HUGIN_CALIB_ERR_INVALID; }
    if (width <= 0 || height <= 0) { set_error(h, "width/height must be > 0"); return HUGIN_CALIB_ERR_INVALID; }
    if (hfov <= 0.0) { set_error(h, "hfov must be > 0"); return HUGIN_CALIB_ERR_INVALID; }

    h->width = width;
    h->height = height;
    h->proj = proj;
    h->hfov = hfov;
    h->crop = crop;
    h->num_images = num_images;

    // 重置每图状态
    h->init_pose.assign(num_images, {0.0, 0.0, 0.0});
    h->opt_vars.assign(num_images, {});
    h->cps.clear();
    h->ran = false;
    h->results.clear();
    h->last_error.clear();
    return HUGIN_CALIB_OK;
}

extern "C" int hugin_calib_set_initial_pose(hugin_calib_handle *h,
                                            int idx,
                                            double yaw, double pitch, double roll)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    if (idx < 0 || idx >= h->num_images) { set_error(h, "image index out of range"); return HUGIN_CALIB_ERR_INVALID; }
    h->init_pose[idx] = {yaw, pitch, roll};
    return HUGIN_CALIB_OK;
}

extern "C" int hugin_calib_load_initial_poses(hugin_calib_handle *h,
                                              const char *filename)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    if (!filename) { set_error(h, "filename is NULL"); return HUGIN_CALIB_ERR_NULL; }
    if (h->num_images == 0) { set_error(h, "call set_images first"); return HUGIN_CALIB_ERR_INVALID; }

    std::ifstream f(filename);
    if (!f) { set_error(h, std::string("cannot open ") + filename); return HUGIN_CALIB_ERR_INIT_FILE; }
    std::string line;
    int idx = 0;
    while (std::getline(f, line) && idx < h->num_images)
    {
        std::istringstream s2(line);
        double yaw, pitch, roll;
        if (s2 >> yaw >> pitch >> roll)
            h->init_pose[idx] = {yaw, pitch, roll};
        ++idx;
    }
    return HUGIN_CALIB_OK;
}

extern "C" int hugin_calib_set_flags(hugin_calib_handle *h, int flags)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    h->flags = flags;
    return HUGIN_CALIB_OK;
}

extern "C" int hugin_calib_set_optimize_vars(hugin_calib_handle *h,
                                             int idx, const char *vars)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    if (!vars) { set_error(h, "vars is NULL"); return HUGIN_CALIB_ERR_NULL; }
    if (idx < 0 || idx >= h->num_images) { set_error(h, "image index out of range"); return HUGIN_CALIB_ERR_INVALID; }

    std::set<std::string> s;
    std::stringstream ss(vars);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        // trim whitespace
        size_t b = item.find_first_not_of(" \t");
        size_t e = item.find_last_not_of(" \t");
        if (b == std::string::npos) continue;
        s.insert(item.substr(b, e - b + 1));
    }
    h->opt_vars[idx] = s;
    return HUGIN_CALIB_OK;
}

// ------------------------------------------------------------------
// 控制点
// ------------------------------------------------------------------

extern "C" int hugin_calib_add_cp(hugin_calib_handle *h,
                                  int img1, double x1, double y1,
                                  int img2, double x2, double y2)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    if (h->num_images == 0) { set_error(h, "call set_images first"); return HUGIN_CALIB_ERR_INVALID; }
    if (img1 < 0 || img1 >= h->num_images || img2 < 0 || img2 >= h->num_images)
    {
        set_error(h, "control point image index out of range");
        return HUGIN_CALIB_ERR_INVALID;
    }
    h->cps.push_back({(double)img1, x1, y1, (double)img2, x2, y2});
    return HUGIN_CALIB_OK;
}

extern "C" int hugin_calib_load_cp_file(hugin_calib_handle *h,
                                        const char *filename)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    if (!filename) { set_error(h, "filename is NULL"); return HUGIN_CALIB_ERR_NULL; }
    if (h->num_images == 0) { set_error(h, "call set_images first"); return HUGIN_CALIB_ERR_INVALID; }

    std::ifstream f(filename);
    if (!f) { set_error(h, std::string("cannot open ") + filename); return HUGIN_CALIB_ERR_CP_FILE; }
    std::string line;
    int ncp = 0;
    while (std::getline(f, line))
    {
        std::istringstream s2(line);
        unsigned int i1, i2;
        double x1, y1, x2, y2;
        if (!(s2 >> i1 >> x1 >> y1 >> i2 >> x2 >> y2)) continue;
        if (i1 >= (unsigned)h->num_images || i2 >= (unsigned)h->num_images) continue;
        h->cps.push_back({(double)i1, x1, y1, (double)i2, x2, y2});
        ++ncp;
    }
    if (ncp == 0) { set_error(h, "no valid control points in file"); return HUGIN_CALIB_ERR_NO_CP; }
    return HUGIN_CALIB_OK;
}

extern "C" int hugin_calib_get_num_cp(const hugin_calib_handle *h)
{
    if (!valid_handle(h)) return 0;
    return (int)h->cps.size();
}

// ------------------------------------------------------------------
// 执行
// ------------------------------------------------------------------

extern "C" int hugin_calib_run(hugin_calib_handle *h)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    if (h->num_images < 2) { set_error(h, "call set_images first (>= 2 images)"); return HUGIN_CALIB_ERR_INVALID; }
    if (h->cps.empty()) { set_error(h, "no control points added"); return HUGIN_CALIB_ERR_NO_CP; }

    const int N = h->num_images;
    const bool doAuto = (h->flags & HUGIN_CALIB_FLAG_AUTO) != 0;
    const bool fixLens = (h->flags & HUGIN_CALIB_FLAG_FIX_LENS) != 0;
    const bool doDistribute = (h->flags & HUGIN_CALIB_FLAG_DISTRIBUTE) != 0;

    // ---- 1. 构建 panorama ----
    Panorama pano;
    const vigra::Size2D size(h->width, h->height);

    // 初始姿态
    std::vector<std::array<double, 3>> initPose = h->init_pose;

    if (doDistribute)
    {
        // 复现 Hugin GUI DistributeImagesCmd: 按 HFOV 均匀分布 yaw
        size_t imgsPerRow;
        if (h->proj == 0) // RECTILINEAR
        {
            imgsPerRow = std::max(3, (int)(360.0 / (0.8 * h->hfov)));
            imgsPerRow = std::min(imgsPerRow, (size_t)N);
        }
        else
        {
            imgsPerRow = N; // 鱼眼等: 一行
        }
        double offset = 0.75 * h->hfov;
        if ((imgsPerRow - 1.0) * offset > 360.0)
            offset = 360.0 / (imgsPerRow - 1.0);
        double yaw = -(imgsPerRow - 1.0) / 2.0 * offset;
        double pitch = 0;
        if (imgsPerRow < (size_t)N)
            pitch = -(std::ceil((double)N / (double)imgsPerRow) - 1.0) / 2.0 * offset;
        size_t counter = 0;
        for (int i = 0; i < N; ++i)
        {
            initPose[i] = {yaw, pitch, 0.0};
            yaw += offset;
            ++counter;
            if (counter == imgsPerRow)
            {
                counter = 0;
                pitch += offset;
                yaw = -(imgsPerRow - 1.0) / 2.0 * offset;
            }
        }
    }

    for (int i = 0; i < N; ++i)
    {
        SrcPanoImage img;
        img.setProjection((BaseSrcPanoImage::Projection)h->proj);
        img.setSize(size);
        img.setHFOV(h->hfov);
        img.setRadialDistortion({0.0, 0.0, 0.0});
        img.setYaw(initPose[i][0]);
        img.setPitch(initPose[i][1]);
        img.setRoll(initPose[i][2]);
        img.setCropFactor(h->crop);
        img.setFilename("camera_" + std::to_string(i) + ".jpg");
        pano.addImage(img);
    }

    // 参考图 = 0 (定义全局坐标系)
    PanoramaOptions opts = pano.getOptions();
    opts.optimizeReferenceImage = 0;
    pano.setOptions(opts);

    // ---- 2. 链接镜头变量 (所有图像共享 v/a/b/c) ----
    StandardImageVariableGroups sigv(pano);
    ImageVariableGroup & lenses = sigv.getLenses();
    for (int i = 1; i < N; ++i)
        lenses.switchParts(i, 0);
    const std::set<ImageVariableGroup::ImageVariableEnum> & lensVars =
        ConstStandardImageVariableGroups::getLensVariables();
    for (auto v : lensVars)
        lenses.linkVariablePart(v, 0);

    // ---- 3. 加载控制点 ----
    for (const auto &cp : h->cps)
    {
        pano.addCtrlPoint(ControlPoint((unsigned)cp[0], cp[1], cp[2],
                                       (unsigned)cp[3], cp[4], cp[5],
                                       ControlPoint::X_Y));
    }

    // ---- 4. 设置优化变量 ----
    OptimizeVector optvec(N);
    const std::set<std::string> emptyLens;
    for (int i = 0; i < N; ++i)
    {
        if (!h->opt_vars[i].empty())
        {
            // 用户显式指定
            for (auto &var : h->opt_vars[i]) optvec[i].insert(var);
        }
        else if (i == 0)
        {
            // 默认: 图 0 优化镜头 (除非 --fix-lens)
            if (!fixLens)
            {
                optvec[i].insert("v");
                optvec[i].insert("a");
                optvec[i].insert("b");
                optvec[i].insert("c");
            }
        }
        else
        {
            // 默认: 其他图优化姿态
            optvec[i].insert("y");
            optvec[i].insert("p");
            optvec[i].insert("r");
        }
    }
    pano.setOptimizeVector(optvec);

    // ---- 5. 运行优化器 ----
    unsigned int ret;
    if (doAuto)
    {
        AutoOptimise::autoOptimise(pano, true);
    }
    ret = PTools::optimize(pano);
    if (ret != 0)
    {
        set_error(h, "PTools::optimize failed (ret=" + std::to_string(ret) + ")");
        return HUGIN_CALIB_ERR_OPTIMIZE;
    }

    // ---- 6. 收集结果 ----
    h->results.clear();
    h->results.reserve(N);
    for (int i = 0; i < N; ++i)
    {
        const SrcPanoImage &im = pano.getImage(i);
        hugin_calib_image_result r;
        std::memset(&r, 0, sizeof(r));
        r.width = im.getSize().width();
        r.height = im.getSize().height();
        r.projection = (int)im.getProjection();
        r.v = im.getVar("v");
        r.a = im.getVar("a");
        r.b = im.getVar("b");
        r.c = im.getVar("c");
        r.yaw = im.getVar("y");
        r.pitch = im.getVar("p");
        r.roll = im.getVar("r");
        std::strncpy(r.filename, im.getFilename().c_str(), sizeof(r.filename) - 1);
        h->results.push_back(r);
    }
    h->rms_error = compute_rms(pano);
    h->ran = true;
    h->last_error.clear();
    return HUGIN_CALIB_OK;
}

// ------------------------------------------------------------------
// 结果
// ------------------------------------------------------------------

extern "C" int hugin_calib_get_result(const hugin_calib_handle *h,
                                      hugin_calib_result *result)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    if (!result) return HUGIN_CALIB_ERR_NULL;
    if (!h->ran) { set_error(const_cast<hugin_calib_handle *>(h), "run() not called yet"); return HUGIN_CALIB_ERR_NOT_RUN; }

    result->num_images = (int)h->results.size();
    result->rms_error = h->rms_error;
    result->images = (hugin_calib_image_result *)malloc(
        h->results.size() * sizeof(hugin_calib_image_result));
    if (!result->images) return HUGIN_CALIB_ERR_INVALID;
    std::memcpy(result->images, h->results.data(),
                h->results.size() * sizeof(hugin_calib_image_result));
    return HUGIN_CALIB_OK;
}

extern "C" int hugin_calib_write_pto(const hugin_calib_handle *h,
                                     const char *filename)
{
    if (!valid_handle(h)) return HUGIN_CALIB_ERR_NULL;
    if (!filename) return HUGIN_CALIB_ERR_NULL;
    if (!h->ran) { set_error(const_cast<hugin_calib_handle *>(h), "run() not called yet"); return HUGIN_CALIB_ERR_NOT_RUN; }

    std::ofstream o(filename);
    if (!o) { set_error(const_cast<hugin_calib_handle *>(h), std::string("cannot write ") + filename); return HUGIN_CALIB_ERR_WRITE; }

    o << "#hugin_optimizeReferenceImage 0\n";
    for (const auto &r : h->results)
    {
        o << "i w" << r.width
          << " h" << r.height
          << " f" << r.projection
          << " v" << r.v
          << " a" << r.a
          << " b" << r.b
          << " c" << r.c
          << " y" << r.yaw
          << " p" << r.pitch
          << " r" << r.roll
          << " n\"" << r.filename << "\"\n";
    }
    o.close();
    return HUGIN_CALIB_OK;
}

extern "C" const char *hugin_calib_last_error(const hugin_calib_handle *h)
{
    if (!valid_handle(h)) return "invalid handle";
    return h->last_error.c_str();
}

// ------------------------------------------------------------------
// 工具
// ------------------------------------------------------------------

extern "C" double hugin_calib_calc_hfov(double focal_mm, double crop_factor,
                                        int width, int height)
{
    // Hugin 公式 (FULL_FRAME_FISHEYE):
    //   sensor_diag = sqrt(36^2 + 24^2) / crop_factor
    //   sensor_w = sensor_diag / sqrt(1 + 1/r^2),  r = width/height
    //   HFOV = sensor_w / focal_mm * 180/pi
    if (focal_mm <= 0.0 || crop_factor <= 0.0 || width <= 0 || height <= 0)
        return 0.0;
    double r = (double)width / (double)height;
    double sensor_diag = std::sqrt(36.0 * 36.0 + 24.0 * 24.0) / crop_factor;
    double sensor_w = sensor_diag / std::sqrt(1.0 + 1.0 / (r * r));
    return sensor_w / focal_mm * 180.0 / M_PI;
}

extern "C" const char *hugin_calib_version(void)
{
    return "1.0.0";
}