# Documentation index

This page is the entry point for everything that does not fit in the main
[README](../README.md): build recipes for embedded targets, the validation methodology, and the
deep-dive documents (currently Simplified Chinese, translations welcome).

**English** · [简体中文](../README.zh-CN.md)

---

## Reading paths

| I want to… | Read |
|---|---|
| Integrate the library into my product | [README → Quick start](../README.md#quick-start) → [README → Recipes](../README.md#recipes) |
| Understand the lens model and optimiser internals | [DOCUMENTATION.md](../DOCUMENTATION.md) §2, §7 |
| Reproduce the Hugin GUI result exactly | [HUGIN_GUI_ALIGNMENT.md](../HUGIN_GUI_ALIGNMENT.md) |
| Use vertical lines / `SMART` optimisation | [LINEFIND_SMART_GUIDE.md](../LINEFIND_SMART_GUIDE.md) |
| Debug mirrored or crossed chessboard points | [CHESSBOARD_CORNER_ALIGNMENT.md](../CHESSBOARD_CORNER_ALIGNMENT.md) |
| Build on RK3588 / aarch64 | [Building on aarch64](#building-on-aarch64) below |
| Judge whether the accuracy claims hold | [Validation method](#validation-method) below |
| Compare against other calibration tools | [README → Related projects](../README.md#related-projects) |

## Document map

| Document | Language | Contents |
|---|---|---|
| [../README.md](../README.md) | EN | project overview, quick start, API, recipes, FAQ |
| [../README.zh-CN.md](../README.zh-CN.md) | 中文 | 主 README 简体中文版 |
| [DOCUMENTATION.md](../DOCUMENTATION.md) | 中文 | 背景与目标、标定算法原理、工程结构、RK3588 编译、命令行、C++ API、AVS 集成、实测结果、常见坑 |
| [HUGIN_GUI_ALIGNMENT.md](../HUGIN_GUI_ALIGNMENT.md) | 中文 | Hugin GUI 标定流程逐项分析、三个关键修复、完全对齐验证、4 路相机流程 |
| [LINEFIND_SMART_GUIDE.md](../LINEFIND_SMART_GUIDE.md) | 中文 | `LINEFIND` / `SMART` 原理、API 与命令行用法、`num_lines` 语义、调参坑 |
| [CHESSBOARD_CORNER_ALIGNMENT.md](../CHESSBOARD_CORNER_ALIGNMENT.md) | 中文 | 棋盘角点镜像/交叉的根因、格子颜色判据、修复流程 |

> The four deep-dive documents are in Simplified Chinese. They are the most detailed material in
> the repository; English translations are an explicitly welcome contribution — see
> [Contributing](../README.md#contributing).

---

## Building on aarch64

Ubuntu for aarch64 has **no VIGRA package**, so VIGRA must be built from source. x86_64 desktops
can install `libvigraimpex-dev` and skip step 1.

### 0. System dependencies

```bash
sudo apt install -y libpano13-dev liblcms2-dev build-essential cmake
```

### 1. Build VIGRA (aarch64)

```bash
git clone --depth 1 --branch Version-1-11-2 \
    https://github.com/ukoethe/vigra.git /root/vigra

# VIGRA 1.11.2 ships a CMake bug: CMakeLists.txt references the doc_cpp target with
#   ADD_DEPENDENCIES(PACKAGE_SRC_TAR doc_cpp)
# outside of IF(BUILD_DOCS). Move that line inside IF(BUILD_DOCS) (around lines 343-347),
# otherwise configuration fails with BUILD_DOCS=OFF.

cd /root/vigra && mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/opt/vigra \
    -DWITH_OPENEXR=OFF -DWITH_HDF5=OFF -DWITH_BOOST_GRAPH=OFF \
    -DWITH_VIGRANUMPY=OFF -DWITH_FFTW=OFF \
    -DWITH_JPEG=OFF -DWITH_PNG=OFF -DWITH_TIFF=OFF -DWITH_ZLIB=OFF \
    -DBUILD_TESTS=OFF -DBUILD_DOCS=OFF ..
make -j4 && make install
# produces /opt/vigra/include/vigra and /opt/vigra/lib/libvigraimpex.so
```

Only VIGRA's core types and two `impex` symbols are needed; every image codec is disabled, which
is what keeps the runtime footprint at four shared libraries.

### 2. Build libhugin_calib

```bash
cmake -S . -B build -DVIGRA_ROOT=/opt/vigra    # omit if VIGRA is in a default prefix
cmake --build build -j4
```

### 3. Package and smoke-test

```bash
./package.sh build dist
export LD_LIBRARY_PATH=$PWD/dist/lib
./dist/bin/example_c dist/examples/cp_sample.txt /tmp/result.pto
```

### aarch64 pitfalls

| symptom | cause / fix |
|---|---|
| `undefined reference to vigra::isImage` | headers-only VIGRA is not enough — link `libvigraimpex.so` |
| `ADD_DEPENDENCIES ... doc_cpp` on configure | the VIGRA CMake bug described above |
| `libpano13.so.3: cannot open shared object file` | `export LD_LIBRARY_PATH=dist/lib`, or set an rpath |
| optimiser diverges on the board | wrong initial HFOV — `v` is an angle, compute it with `hugin_calib_calc_hfov()` |

The compile-time trimming relative to upstream Hugin (which directories are removed, which files
are patched, which stubs were added) is documented in
[DOCUMENTATION.md §4.4](../DOCUMENTATION.md).

---

## Validation method

The accuracy claims in the main README were established with the same dataset in three
independent ways.

**Dataset.** 3 cameras, 4K full-frame fisheye, `f = 2.95 mm`, crop factor `4.85`
(`HFOV = 151.013868°`), 15 control points (11 for cam0↔cam1, 4 for cam1↔cam2), run with
`--auto --fix-lens --distribute`.

**Cross-checks.**

1. `hugin_calib_driver` — the library under test.
2. `PToptimizer` from libpano13 — the reference optimiser binary, fed the same script.
3. `verify_pto` (`src/verify_pto.cpp`) — a direct `ParseScript` / `RunLMOptimizer` harness with no
   PanoramaModel involvement.

All three agree on the resulting script. The comparison against the Hugin GUI output is a
per-field diff; the largest deviation is below `0.001°`, which is the granularity of the
6-decimal output format rather than an optimiser difference.

**Reference `.pto` caveat.** The official `.pto` in that dataset carries *hand-set ideal* poses.
Fed as an initial guess it starts at RMS 84.86 px and the optimiser converges to 6.68 px — so the
file is a ground-truth target, not a converged state.

**Known limits.**

- **FOV / pose coupling.** With few control points, HFOV and pose trade off against each other;
  synthetic rectilinear tests recover yaw to 2–3° and HFOV to ~6 % unless the initial HFOV is
  realistic. Always seed `hfov` from `hugin_calib_calc_hfov()`.
- **Control-point noise floor.** Real 4K chessboard sets converge to RMS ≈ 5.6 px, set by corner
  detection, not by the optimiser.
- **Vertical lines** only constrain `roll`/`pitch`; yaw and HFOV still need point correspondences.
- **Output precision.** The pto writer prints 6 decimals (`%.6f`); differences below that cannot
  be compared field-by-field.

To reproduce: `build/verify_pto`, `build/hugin_calib_driver --help`, and the pto files in the
repository root (`rk_3cam_calib.pto`, `verify_gui_pose.pto`, `result_3cam_sync_fixed.pto`).

---

## Runtime bundle layout

`./package.sh build dist` produces a relocatable tree; nothing needs to be installed on the
target machine:

```text
dist/
├── lib/          libhugin_calib.so, libpano13.so.3, liblcms2.so.2, libvigraimpex.so.11
├── include/      hugin_calib.h
├── bin/          hugin_calib_driver, example_c, example_cpp, verify_pto
├── examples/     sources + cp_sample.txt
├── README.md
├── LICENSE
└── docs/README.md
```

`libstdc++`, `libc`, `libm` and `libgcc_s` are deliberately not bundled — every target has them.
