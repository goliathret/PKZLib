#include "AnimationTypes.h"
#include "detail/AnimationMath.h"

using pkzlib::animation_detail::Normalize;

BUMatrix CMTransforms::Matrix() const
{
    const ANValue q = Normalize(rotation);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    BUMatrix m;
    m.m[0][0] = (1 - 2 * y * y - 2 * z * z) * scale[0];
    m.m[0][1] = (2 * x * y + 2 * z * w) * scale[0];
    m.m[0][2] = (2 * x * z - 2 * y * w) * scale[0];
    m.m[1][0] = (2 * x * y - 2 * z * w) * scale[1];
    m.m[1][1] = (1 - 2 * x * x - 2 * z * z) * scale[1];
    m.m[1][2] = (2 * y * z + 2 * x * w) * scale[1];
    m.m[2][0] = (2 * x * z + 2 * y * w) * scale[2];
    m.m[2][1] = (2 * y * z - 2 * x * w) * scale[2];
    m.m[2][2] = (1 - 2 * x * x - 2 * y * y) * scale[2];
    m.m[3][0] = translation[0];
    m.m[3][1] = translation[1];
    m.m[3][2] = translation[2];
    return m;
}
BUMatrix CMTransforms::InverseMatrix() const
{
    CMTransforms inverse;
    inverse.rotation = {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    BUMatrix result = inverse.Matrix();
    for (int row = 0; row < 3; ++row)
    {
        if (!std::isfinite(scale[row]) || std::abs(scale[row]) < 1e-8f)
            throw std::runtime_error("Cannot invert a transform with a singular scale.");
        for (int column = 0; column < 3; ++column)
            result.m[column][row] /= scale[row];
        result.m[3][row] = -(result.m[0][row] * translation[0] + result.m[1][row] * translation[1] +
                             result.m[2][row] * translation[2]);
    }
    return result;
}
