#pragma once
#include "../AnimationTypes.h"
#include <cmath>
#include <stdexcept>

namespace pkzlib::animation_detail
{
inline ANValue Normalize(ANValue q)
{
    float n = 0;
    for (float x : q)
        n += x * x;
    if (!std::isfinite(n) || n < 1e-12f)
        throw std::runtime_error("Animation contains an invalid quaternion.");
    for (float& x : q)
        x /= std::sqrt(n);
    return q;
}
} // namespace pkzlib::animation_detail
