// GPU remapping stubs for the minimal (no-OpenGL) hugin build.
//
// The GPU code path is never taken at runtime (remapUsingGPU=false in the
// minimal build), but the non-template functions below are referenced by the
// templated GPU code in ImageTransformsGPU.h / RemappedPanoImage.h, so we
// provide them to satisfy the linker. If they are ever called, we throw.

#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>

#include <vigra/diff2d.hxx>
#include "vigra_ext/ImageTransformsGPU.h"
#include "panotools/PanoToolsInterface.h"

namespace vigra_ext
{

bool transformImageGPUIntern(const std::string& coordXformGLSL,
                             const std::string& interpolatorGLSL,
                             const int interpolatorSize,
                             const std::string& photometricGLSL,
                             const std::vector<double>& invLut,
                             const std::vector<double>& destLut,
                             const vigra::Diff2D srcSize,
                             const void* const srcBuffer,
                             const int srcGLInternalFormat, const int srcGLTransferFormat, const int srcGLFormat, const int srcGLType,
                             const void* const srcAlphaBuffer,
                             const int srcAlphaGLType,
                             const vigra::Diff2D destUL,
                             const vigra::Diff2D destSize,
                             void* const destBuffer,
                             const int destGLInternalFormat, const int destGLTransferFormat, const int destGLFormat, const int destGLType,
                             void* const destAlphaBuffer,
                             const int destAlphaGLType,
                             const bool warparound)
{
    throw std::runtime_error("hugin_min: GPU remapping not supported (OpenGL not linked)");
}

} // namespace vigra_ext

namespace HuginBase
{
namespace PTools
{

bool Transform::emitGLSL(std::ostringstream& oss) const
{
    throw std::runtime_error("hugin_min: GPU remapping not supported (OpenGL not linked)");
}

} // namespace PTools
} // namespace HuginBase
