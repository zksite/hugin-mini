// hugin_min stubs
//
// Minimal placeholder implementations for Hugin functions that the core
// panodata code references, but that are only meaningful in the full GUI /
// HDR stacking workflow. The calibration path never needs the real
// implementation of these, so a minimal stub keeps the build minimal.

#include <panodata/PanoramaData.h>

namespace HuginBase
{

// Real implementation lives in algorithms/basic/LayerStacks.cpp and groups
// images into exposure layers (bracketing). Calibration does not need that:
// treat all images as one layer.
UIntSetVector getExposureLayers(const PanoramaData & pano, UIntSet allImgs, const double maxEVDiff)
{
    (void)pano;
    (void)maxEVDiff;
    UIntSetVector layers;
    layers.push_back(allImgs);
    return layers;
}

} // namespace HuginBase
