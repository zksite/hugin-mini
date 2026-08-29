// hugin_min calibration driver
//
// Minimal Hugin-based lens calibration using checkerboard control points.
// Builds a panorama, feeds 2D correspondences, runs the Levenberg-Marquardt
// optimizer (libpano13 + bundled levmar) and writes an AVS-friendly pto
// containing the optimized geometric parameters (v/a/b/c/y/p/r).
//
// Usage:
//   hugin_calib_driver --width 3840 --height 2160 --proj 3 --hfov 151 \
//       --crop 4.85 --images 4 --cp control_points.txt --out result.pto
//
// Control point file format (one correspondence per line):
//   img1 x1 y1 img2 x2 y2      (0-based image indices, pixel coordinates)
// Lines starting with '#' are ignored.
//
// To replicate Hugin GUI "Positions (incremental, starting from anchor)":
//   --auto --fix-lens   (auto-optimise pairwise + global, lens v/a/b/c fixed)

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <array>
#include <cstdlib>

#include <panodata/Panorama.h>
#include <panodata/SrcPanoImage.h>
#include <panodata/ControlPoint.h>
#include <panodata/PanoramaOptions.h>
#include <panodata/StandardImageVariableGroups.h>
#include <panotools/PanoToolsOptimizerWrapper.h>
#include <algorithms/optimizer/PTOptimizer.h>
#include <lines/FindLines.h>
#include <vigra/impex.hxx>
#include <vigra_ext/impexalpha.hxx>
#include <vigra/functorexpression.hxx>
#include <vigra_ext/utils.h>

using namespace HuginBase;

// ================= linefind support (Hugin assistant "Align") =================
// Replicates the `linefind` step of the Hugin assistant: detects vertical lines
// in each image and adds them as t1 (ControlPoint::X) control points, which
// constrain roll/pitch during optimization.

static std::string findImagePath(const std::string& dir, int i)
{
    for (const char* ext : {".png", ".jpg", ".jpeg"})
    {
        std::string p = dir + "/camera_" + std::to_string(i) + ext;
        std::ifstream f(p.c_str());
        if (f.good()) return p;
    }
    return "";
}

static std::string findImageName(const std::string& dir, int i)
{
    std::string p = findImagePath(dir, i);
    if (!p.empty())
    {
        size_t pos = p.find_last_of('/');
        return (pos == std::string::npos) ? p : p.substr(pos + 1);
    }
    return "camera_" + std::to_string(i) + ".jpg";
}

// load gray image (UINT8 fast path) into BImage
static vigra::BImage loadGrayImage(vigra::ImageImportInfo& info, vigra::BImage& mask)
{
    vigra::BImage image(info.width(), info.height());
    if (info.numExtraBands() == 1)
    {
        mask.resize(info.width(), info.height());
        vigra::importImageAlpha(info, vigra::destImage(image), vigra::destImage(mask));
    }
    else
    {
        vigra::importImage(info, vigra::destImage(image));
    }
    return image;
}

// load color image (UINT8 fast path) into UInt8RGBImage
static vigra::UInt8RGBImage loadColorImage(vigra::ImageImportInfo& info, vigra::BImage& mask)
{
    vigra::UInt8RGBImage image(info.width(), info.height());
    if (info.numExtraBands() == 1)
    {
        mask.resize(info.width(), info.height());
        vigra::importImageAlpha(info, vigra::destImage(image), vigra::destImage(mask));
    }
    else
    {
        vigra::importImage(info, vigra::destImage(image));
    }
    return image;
}

static void usage(const char* prog)
{
    std::cerr <<
        "Usage: " << prog << " [options]\n"
        "  --width  W        image width in px (default 3840)\n"
        "  --height H        image height in px (default 2160)\n"
        "  --proj  P         projection f-code (0=rect,2=circ fisheye,3=full-frame fisheye,...) default 3\n"
        "  --hfov V          initial HFOV in degrees (default 150)\n"
        "  --crop C          crop factor, metadata only (default 1.0)\n"
        "  --images N        number of images (default 2)\n"
        "  --cp FILE         control point file: lines 'img1 x1 y1 img2 x2 y2'\n"
        "  --opt LIST        variables to optimize for non-reference images,\n"
        "                    comma separated (default 'v,a,b,c' for image 0 and\n"
        "                    'y,p,r' for the rest)\n"
        "  --init FILE       initial yaw/pitch/roll per image, one line per image:\n"
        "                    'yaw pitch roll' (degrees, default all 0)\n"
        "  --auto            run AutoOptimise (incremental pairwise) before global\n"
        "                    optimization, like Hugin's 'Positions (incremental)' mode\n"
        "  --smart           run SmartOptimise (assistant \"Align\", autooptimiser -a).\n"
        "                    AutoOptimise + global position + lens calibration:\n"
        "                    optimizes v/a/b/c/d/e when pano FOV >= 150 deg.\n"
        "                    Replicates the Hugin assistant that changed HFOV.\n"
        "  --fix-lens        do not optimize lens variables (v/a/b/c), keep them fixed.\n"
        "                    Use with --auto to replicate Hugin GUI 'Positions (incremental)'\n"
        "  --distribute      auto-distribute initial yaw like Hugin GUI 'DistributeImagesCmd'\n"
        "                    (spreads images evenly based on HFOV). Use with --auto --fix-lens\n"
        "                    to fully replicate Hugin GUI 'Positions (incremental, from anchor)'\n"
        "  --linefind        detect vertical lines in each image and add them as\n"
        "                    t1 (vertical line) control points to constrain roll,\n"
        "                    like the Hugin assistant 'linefind' step. Needs the\n"
        "                    actual image files. Combine with --smart.\n"
        "  --lines N         max vertical lines per image for --linefind (default 5)\n"
        "  --imgdir DIR      directory containing camera_<i>.png/.jpg for --linefind\n"
        "                    (default '.', filename written to pto is auto-detected)\n"
        "  --out FILE        output pto filename (default calib_result.pto)\n";
}

int main(int argc, char** argv)
{
    int W = 3840, H = 2160, proj = 3, N = 2;
    double hfov = 150.0, crop = 1.0;
    std::string cpFile, outFile = "calib_result.pto", initFile;
    bool doAuto = false, fixLens = false, doDistribute = false, doSmart = false, doLinefind = false;
    int nrLines = 5;
    std::string imgdir = ".";
    // variables to optimize: [0] for image 0, [1] for the others
    std::set<std::string> optLens = {"v","a","b","c"};
    std::set<std::string> optPose = {"y","p","r"};

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) { std::cerr << "missing value for " << name << "\n"; exit(1); }
            return argv[++i];
        };
        if      (a == "--width")  W     = atoi(next("--width").c_str());
        else if (a == "--height") H     = atoi(next("--height").c_str());
        else if (a == "--proj")   proj  = atoi(next("--proj").c_str());
        else if (a == "--hfov")   hfov  = atof(next("--hfov").c_str());
        else if (a == "--crop")   crop  = atof(next("--crop").c_str());
        else if (a == "--images") N     = atoi(next("--images").c_str());
        else if (a == "--cp")     cpFile = next("--cp");
        else if (a == "--out")    outFile = next("--out");
        else if (a == "--init")   initFile = next("--init");
        else if (a == "--auto")   doAuto = true;
        else if (a == "--smart")  doSmart = true;
        else if (a == "--linefind") doLinefind = true;
        else if (a == "--lines")  nrLines = atoi(next("--lines").c_str());
        else if (a == "--imgdir") imgdir = next("--imgdir");
        else if (a == "--fix-lens") fixLens = true;
        else if (a == "--distribute") doDistribute = true;
        else if (a == "--opt")
        {
            // comma separated, e.g. "v,y,p,r" -> lens vars off for image 0,
            // pose vars as given. 'v,a,b,c' always go to image 0 (the lens).
            std::string list = next("--opt");
            std::stringstream ss(list);
            std::string item;
            std::set<std::string> all;
            while (std::getline(ss, item, ',')) if (!item.empty()) all.insert(item);
            optLens.clear(); optPose.clear();
            for (auto & x : all)
            {
                if (x=="v"||x=="a"||x=="b"||x=="c") optLens.insert(x);
                else if (x=="y"||x=="p"||x=="r") optPose.insert(x);
            }
        }
        else if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else { std::cerr << "unknown option: " << a << "\n"; usage(argv[0]); return 1; }
    }

    if (N < 2) { std::cerr << "need at least 2 images\n"; return 1; }

    // ---- 1. build panorama ----
    Panorama pano;
    const vigra::Size2D size(W, H);
    // optional initial yaw/pitch/roll per image
    std::vector<std::array<double,3>> initPose(N, {0.0, 0.0, 0.0});
    if (!initFile.empty())
    {
        std::ifstream fi(initFile);
        if (!fi) { std::cerr << "cannot open " << initFile << "\n"; return 1; }
        std::string line;
        int idx = 0;
        while (std::getline(fi, line) && idx < N)
        {
            std::istringstream s2(line);
            double yaw, pitch, roll;
            if (s2 >> yaw >> pitch >> roll)
                initPose[idx] = {yaw, pitch, roll};
            ++idx;
        }
        std::cout << "loaded initial poses from " << initFile << "\n";
    }
    if (doDistribute)
    {
        // Replicate Hugin GUI DistributeImagesCmd: spread images evenly by yaw.
        // For non-rectilinear (fisheye etc.) all images go in one row.
        size_t imgsPerRow;
        if (proj == 0) // RECTILINEAR
        {
            imgsPerRow = std::max(3, (int)(360.0 / (0.8 * hfov)));
            imgsPerRow = std::min(imgsPerRow, (size_t)N);
        }
        else
        {
            imgsPerRow = N; // fisheye etc.: one row
        }
        double offset = 0.75 * hfov;
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
        std::cout << "distributed initial yaw (offset=" << offset << " deg)\n";
    }
    for (int i = 0; i < N; ++i)
    {
        SrcPanoImage img;
        img.setProjection((BaseSrcPanoImage::Projection)proj);
        img.setSize(size);
        img.setHFOV(hfov);                       // v initial value
        img.setRadialDistortion({0.0, 0.0, 0.0}); // a,b,c initial value
        img.setYaw(initPose[i][0]);
        img.setPitch(initPose[i][1]);
        img.setRoll(initPose[i][2]);
        img.setCropFactor(crop);                 // metadata only
        img.setFilename(findImageName(imgdir, i));
        pano.addImage(img);
    }

    // reference image = 0 (defines the global frame)
    PanoramaOptions opts = pano.getOptions();
    opts.optimizeReferenceImage = 0;
    pano.setOptions(opts);

    // ---- 2. link lens variables across all images (v=0 a=0 ...) ----
    StandardImageVariableGroups sigv(pano);
    ImageVariableGroup & lenses = sigv.getLenses();
    // freshly added images are each in their own lens part by default;
    // move all of them into image 0's part first, then link the variables
    for (int i = 1; i < N; ++i)
        lenses.switchParts(i, 0);
    const std::set<ImageVariableGroup::ImageVariableEnum> & lensVars =
        ConstStandardImageVariableGroups::getLensVariables();
    for (auto v : lensVars)
        lenses.linkVariablePart(v, 0);

    // ---- 3. load control points ----
    if (cpFile.empty()) { std::cerr << "no --cp file given\n"; return 1; }
    std::ifstream f(cpFile);
    if (!f) { std::cerr << "cannot open " << cpFile << "\n"; return 1; }
    std::string line;
    int ncp = 0;
    while (std::getline(f, line))
    {
        std::istringstream s2(line);
        unsigned int i1, i2;
        double x1, y1, x2, y2;
        int t = 0;
        if (!(s2 >> i1 >> x1 >> y1 >> i2 >> x2 >> y2)) continue;
        s2 >> t;   // optional cp type: 0=X_Y (default), 1=vertical line, 2=horizontal line
        if (i1 >= (unsigned)N || i2 >= (unsigned)N) continue;
        int mode = ControlPoint::X_Y;
        if (t == 1) mode = ControlPoint::X;   // VERTICAL_LINE: two points in the same image
        else if (t == 2) mode = ControlPoint::Y; // HORIZONTAL_LINE
        pano.addCtrlPoint(ControlPoint(i1, x1, y1, i2, x2, y2, mode));
        ++ncp;
    }
    std::cout << "loaded " << ncp << " control points\n";
    if (ncp == 0) { std::cerr << "no valid control points\n"; return 1; }

    // ---- 3b. optional: detect vertical lines (like Hugin assistant linefind) ----
    int nvl = 0;
    if (doLinefind)
    {
        std::cout << "detecting vertical lines in images (linefind)...\n";
        for (int i = 0; i < N; ++i)
        {
            std::string fpath = findImagePath(imgdir, i);
            if (fpath.empty())
            {
                std::cerr << "  warning: no image found for camera_" << i
                          << " in " << imgdir << ", skipping\n";
                continue;
            }
            try
            {
                vigra::ImageImportInfo info(fpath.c_str());
                vigra::BImage mask;
                HuginBase::CPVector lines;
                if (info.isGrayscale())
                {
                    vigra::BImage image = loadGrayImage(info, mask);
                    lines = HuginLines::GetVerticalLines(pano, i, image, mask, nrLines);
                }
                else
                {
                    vigra::UInt8RGBImage image = loadColorImage(info, mask);
                    lines = HuginLines::GetVerticalLines(pano, i, image, mask, nrLines);
                }
                for (auto & cp : lines)
                {
                    pano.addCtrlPoint(cp);
                    ++nvl;
                }
                std::cout << "  image " << i << " (" << fpath << "): "
                          << lines.size() << " vertical lines\n";
            }
            catch (std::exception & e)
            {
                std::cerr << "  linefind error on " << fpath << ": " << e.what() << "\n";
            }
        }
        std::cout << "added " << nvl << " vertical line control points\n";
    }

    // ---- 4. set optimize vector ----
    // image 0 (reference + shared lens): optimize lens vars (unless --fix-lens)
    // other images: optimize orientation
    OptimizeVector optvec(N);
    const std::set<std::string> emptyLens;
    const std::set<std::string> & lensSet = fixLens ? emptyLens : optLens;
    for (int i = 0; i < N; ++i)
    {
        const std::set<std::string> & s = (i == 0) ? lensSet : optPose;
        for (auto & var : s) optvec[i].insert(var);
    }
    pano.setOptimizeVector(optvec);

    // ---- 5. run the optimizer (libpano13 LM inside) ----
    unsigned int ret = 0;
    if (doSmart)
    {
        // Hugin assistant "Align" step (autooptimiser -a): SmartOptimise.
        // = AutoOptimise + global OPT_POS + lens calibration, where lens
        // distortion (a/b/c/d/e) and HFOV (v) are optimized when the
        // panorama FOV >= 150 deg. This is what changed v from 151 -> 133.4.
        std::cout << "running SmartOptimise (assistant Align)...\n";
        SmartOptimise::smartOptimize(pano);
        std::cout << "SmartOptimise done\n";
    }
    else if (doAuto)
    {
        // Hugin's "Positions (incremental, starting from anchor)" mode:
        // BFS pairwise pre-optimization, then global optimization
        std::cout << "running AutoOptimise (incremental pairwise)...\n";
        AutoOptimise::autoOptimise(pano, true);
        std::cout << "AutoOptimise done\n";
        ret = PTools::optimize(pano);
        std::cout << "PTools::optimize returned " << ret
                  << " (0=ok, 1=parser error, 2=parameter error)\n";
        if (ret != 0) return 1;
    }
    else
    {
        ret = PTools::optimize(pano);
        std::cout << "PTools::optimize returned " << ret
                  << " (0=ok, 1=parser error, 2=parameter error)\n";
        if (ret != 0) return 1;
    }

    // ---- 6. write AVS-friendly pto (geometric fields only) ----
    std::ofstream o(outFile);
    if (!o) { std::cerr << "cannot write " << outFile << "\n"; return 1; }
    o << "#hugin_optimizeReferenceImage 0\n";
    for (int i = 0; i < N; ++i)
    {
        const SrcPanoImage & im = pano.getImage(i);
        o << "i w" << im.getSize().width()
          << " h" << im.getSize().height()
          << " f" << (int)im.getProjection()
          << " v" << im.getVar("v")
          << " a" << im.getVar("a")
          << " b" << im.getVar("b")
          << " c" << im.getVar("c")
          << " y" << im.getVar("y")
          << " p" << im.getVar("p")
          << " r" << im.getVar("r")
          << " n\"" << im.getFilename() << "\"\n";
    }
    o.close();
    std::cout << "wrote " << outFile << "\n";
    return 0;
}
