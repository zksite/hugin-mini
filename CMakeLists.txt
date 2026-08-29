cmake_minimum_required(VERSION 3.15)
project(hugin_min LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- external dependencies (minimal: libpano13 + lcms2 only) ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(PANO13 REQUIRED IMPORTED_TARGET libpano13)
pkg_check_modules(LCMS2  REQUIRED IMPORTED_TARGET lcms2)

# ---- VIGRA (built from source, minimal codecs-off build installed at /opt/vigra) ----
set(VIGRA_ROOT "/opt/vigra" CACHE PATH "VIGRA install prefix")
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hugin_base
  ${VIGRA_ROOT}/include
)

find_package(Threads REQUIRED)

# ---- bundled Levenberg-Marquardt (pure C, self-contained) ----
add_library(huginlevmar STATIC
  src/levmar/lm.c
  src/levmar/Axb.c
  src/levmar/misc.c
)
set_property(TARGET huginlevmar PROPERTY POSITION_INDEPENDENT_CODE ON)

# ---- minimal Hugin base (geometry + optimizer only) ----
add_library(huginbase STATIC
  src/hugin_base/panodata/Panorama.cpp
  src/hugin_base/panodata/PTScriptParsing.cpp
  src/hugin_base/panodata/PanoramaOptions.cpp
  src/hugin_base/panodata/PanoramaVariable.cpp
  src/hugin_base/panodata/SrcPanoImage.cpp
  src/hugin_base/panodata/Lens.cpp
  src/hugin_base/panodata/ControlPoint.cpp
  src/hugin_base/panodata/Mask.cpp
  src/hugin_base/panodata/ImageVariableGroup.cpp
  src/hugin_base/panodata/StandardImageVariableGroups.cpp
  src/hugin_base/panodata/ParseExp.cpp
  src/hugin_base/panotools/PanoToolsInterface.cpp
  src/hugin_base/panotools/PanoToolsOptimizerWrapper.cpp
  src/hugin_base/algorithms/basic/CalculateOverlap.cpp
  src/hugin_base/algorithms/basic/CalculateCPStatistics.cpp
  src/hugin_base/algorithms/basic/LayerStacks.cpp
  src/hugin_base/algorithms/nona/CalculateFOV.cpp
  src/hugin_base/algorithms/nona/CenterHorizontally.cpp
  src/hugin_base/algorithms/nona/ComputeImageROI.cpp
  src/hugin_base/algorithms/basic/RotatePanorama.cpp
  src/hugin_base/algorithms/basic/CalculateOptimalROI.cpp
  src/hugin_base/algorithms/nona/FitPanorama.cpp
  src/hugin_base/lines/FindLines.cpp
  src/hugin_base/lines/FindN8Lines.cpp
  src/hugin_base/nona/StitcherOptions.cpp
  src/hugin_base/vigra_ext/emor.cpp
  src/hugin_base/gpu_stubs.cpp
  src/hugin_base/appbase/ProgressDisplay.cpp
  src/hugin_base/hugin_math/hugin_math.cpp
  src/hugin_base/hugin_math/Matrix3.cpp
  src/hugin_base/hugin_math/Vector3.cpp
  src/hugin_base/hugin_math/eig_jacobi.cpp
  src/hugin_base/hugin_utils/utils.cpp
  src/hugin_base/hugin_utils/alphanum.cpp
  src/hugin_base/algorithms/optimizer/PTOptimizer.cpp
  src/hugin_base/algorithms/optimizer/ImageGraph.cpp
  src/stubs.cpp
)
set_property(TARGET huginbase PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(huginbase
  huginlevmar
  PkgConfig::PANO13
  PkgConfig::LCMS2
  Threads::Threads
  ${VIGRA_ROOT}/lib/libvigraimpex.so
  m
)

# ---- calibration driver ----
add_executable(hugin_calib_driver src/driver.cpp)
target_link_libraries(hugin_calib_driver huginbase)

# ---- verify_pto: 直接用 libpano13 优化官方 pto 文件 ----
add_executable(verify_pto src/verify_pto.cpp)
target_link_libraries(verify_pto huginbase)

# ==================================================================
# 动态库: libhugin_calib.so — 纯 C API, 可被任意语言调用
# ==================================================================
add_library(hugin_calib SHARED
  src/api/hugin_calib.cpp
)
set_property(TARGET hugin_calib PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET hugin_calib PROPERTY CXX_VISIBILITY_PRESET hidden)
set_property(TARGET hugin_calib PROPERTY VISIBILITY_INLINES_HIDDEN ON)

target_include_directories(hugin_calib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/api
)
target_link_libraries(hugin_calib huginbase)

# ---- 示例程序 ----
add_executable(example_c src/examples/example_c.c)
target_link_libraries(example_c hugin_calib)

add_executable(example_cpp src/examples/example_cpp.cpp)
target_link_libraries(example_cpp hugin_calib)
