#pragma once
#include "BUMath.h"

inline BUVector3 BUBezierInterpolate(const BUVector3& _p0, const BUVector3& _p1, const BUVector3& _p2,
                                     const BUVector3& _p3, float _t)
{
    const float u = 1.0f - _t;
    const float b0 = u * u * u;
    const float b1 = 3.0f * u * u * _t;
    const float b2 = 3.0f * u * _t * _t;
    const float b3 = _t * _t * _t;
    return _p0 * b0 + _p1 * b1 + _p2 * b2 + _p3 * b3;
}

inline float BUBezierInterpolate(float _p0, float _p1, float _p2, float _p3, float _t)
{
    const float u = 1.0f - _t;
    return _p0 * (u * u * u) + _p1 * (3.0f * u * u * _t) + _p2 * (3.0f * u * _t * _t) + _p3 * (_t * _t * _t);
}
