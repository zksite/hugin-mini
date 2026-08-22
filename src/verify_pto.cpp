// verify_pto.cpp — 直接用 libpano13 优化官方 pto 文件
// 保留官方 pto 的完整优化变量列表, 看 Hugin 会得到什么结果
// 用法: verify_pto <input.pto> <output.pto>
// libpano13 的 PTcommon.h 用 #ifndef min 定义 min/max 宏, 会破坏 C++ 标准库。
// 方案: 用 push_macro/pop_macro 保护, 包含 libpano13 头后立即恢复。
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>  // 先包含, 让 filter.h 的 <math.h> 走 include guard 不重复展开

#pragma push_macro("min")
#pragma push_macro("max")
#define min
#define max
extern "C" {
#include <pano13/panorama.h>
#include <pano13/filter.h>
}
#pragma pop_macro("min")
#pragma pop_macro("max")

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <input.pto> <output.pto>\n";
        return 1;
    }
    // 读入 pto 文件
    std::ifstream f(argv[1]);
    if (!f) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string script = ss.str();

    OptInfo opt;
    AlignInfo ainf;
    memset(&opt, 0, sizeof(opt));
    memset(&ainf, 0, sizeof(ainf));

    char* s = strdup(script.c_str());
    int ret = ParseScript(s, &ainf);
    if (ret != 0) {
        std::cerr << "ParseScript failed: " << ret << "\n";
        free(s);
        return 1;
    }
    std::cout << "ParseScript OK: " << ainf.numIm << " images, "
              << ainf.numPts << " control points, "
              << ainf.numParam << " params\n";

    ret = CheckParams(&ainf);
    if (ret != 0) {
        std::cerr << "CheckParams failed: " << ret << "\n";
        free(s);
        return 1;
    }
    std::cout << "CheckParams OK\n";

    ainf.fcn = fcnPano;
    SetGlobalPtr(&ainf);

    opt.numVars = ainf.numParam;
    opt.numData = ainf.numPts;
    opt.SetVarsToX = SetLMParams;
    opt.SetXToVars = SetAlignParams;
    opt.fcn = ainf.fcn;
    *opt.message = 0;

    // 打印优化前姿态
    std::cout << "--- before ---\n";
    for (int i = 0; i < ainf.numIm; ++i) {
        std::cout << "img" << i << ": yaw=" << ainf.im[i].yaw
                  << " pitch=" << ainf.im[i].pitch
                  << " roll=" << ainf.im[i].roll
                  << " hfov=" << ainf.im[i].hfov << "\n";
    }

    RunLMOptimizer(&opt);
    std::cout << "RunLMOptimizer done, message: " << opt.message << "\n";

    // 打印优化后姿态
    std::cout << "--- after ---\n";
    for (int i = 0; i < ainf.numIm; ++i) {
        std::cout << "img" << i << ": yaw=" << ainf.im[i].yaw
                  << " pitch=" << ainf.im[i].pitch
                  << " roll=" << ainf.im[i].roll
                  << " hfov=" << ainf.im[i].hfov << "\n";
    }

    // 写回 pto (用 WriteResults)
    fullPath path;
    StringtoFullPath(&path, argv[2]);
    WriteResults(s, &path, &ainf, distSquared, 0);
    std::cout << "wrote " << argv[2] << "\n";

    DisposeAlignInfo(&ainf);
    free(s);
    return 0;
}