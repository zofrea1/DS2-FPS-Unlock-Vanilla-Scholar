#include "aspect.h"

#include <cmath>

namespace {

constexpr float kWide = 16.0f / 9.0f;

}  // namespace

float aspect_vert_plus(float fovy, float aspect) {
    if (!(aspect > 0.5f && aspect < kWide - 0.005f) || !(fovy > 0.05f && fovy < 3.0f)) {
        return fovy;
    }
    // tan(new/2) * aspect = tan(old/2) * 16/9: the horizontal extent at 16:9 is kept.
    return 2.0f * std::atan(std::tan(fovy * 0.5f) * kWide / aspect);
}
