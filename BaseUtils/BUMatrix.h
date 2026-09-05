#pragma once

#include <cmath>

#include "BUMath.h"

class BUMatrix
{
public:
    static constexpr float kEpsilon = 0.0000099999997f;

    float m[4][4];

    BUMatrix()
    {
        SetIdentity();
    }

    BUMatrix(float _f00, float _f01, float _f02, float _f03,
        float _f10, float _f11, float _f12, float _f13,
        float _f20, float _f21, float _f22, float _f23,
        float _f30, float _f31, float _f32, float _f33)
    {
        m[0][0] = _f00; m[0][1] = _f01; m[0][2] = _f02; m[0][3] = _f03;
        m[1][0] = _f10; m[1][1] = _f11; m[1][2] = _f12; m[1][3] = _f13;
        m[2][0] = _f20; m[2][1] = _f21; m[2][2] = _f22; m[2][3] = _f23;
        m[3][0] = _f30; m[3][1] = _f31; m[3][2] = _f32; m[3][3] = _f33;
    }

    BUMatrix(const BUVector3& _right, const BUVector3& _up, const BUVector3& _forward)
    {
        SetIdentity();
        m[0][0] = _right.x;   m[0][1] = _right.y;   m[0][2] = _right.z;
        m[1][0] = _up.x;      m[1][1] = _up.y;      m[1][2] = _up.z;
        m[2][0] = _forward.x; m[2][1] = _forward.y; m[2][2] = _forward.z;
    }

    void SetIdentity()
    {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                m[r][c] = (r == c) ? 1.0f : 0.0f;
    }

    static BUMatrix FromQuat(const BUQuaternion& q)
    {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        return BUMatrix(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f,
                        2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f,
                        2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f,
                        0.0f,                    0.0f,                    0.0f,                    1.0f);
    }

    void ToQuat(BUQuaternion& _out) const
    {
        const float trace = m[0][0] + m[1][1] + m[2][2];
        if (trace > 0.0f)
        {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            _out.Set((m[1][2] - m[2][1]) / s, (m[2][0] - m[0][2]) / s, (m[0][1] - m[1][0]) / s, 0.25f * s);
        }
        else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
        {
            const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
            _out.Set(0.25f * s, (m[1][0] + m[0][1]) / s, (m[2][0] + m[0][2]) / s, (m[1][2] - m[2][1]) / s);
        }
        else if (m[1][1] > m[2][2])
        {
            const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
            _out.Set((m[1][0] + m[0][1]) / s, 0.25f * s, (m[2][1] + m[1][2]) / s, (m[2][0] - m[0][2]) / s);
        }
        else
        {
            const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
            _out.Set((m[2][0] + m[0][2]) / s, (m[2][1] + m[1][2]) / s, 0.25f * s, (m[0][1] - m[1][0]) / s);
        }
    }

    void Decompose(BUVector3& _outScale, BUQuaternion& _outRot, BUVector3& _outTrans) const
    {
        BUVector3 right(m[0][0], m[0][1], m[0][2]);
        BUVector3 up(m[1][0], m[1][1], m[1][2]);
        BUVector3 forward(m[2][0], m[2][1], m[2][2]);

        _outScale.x = right.Length();
        _outScale.y = up.Length();
        _outScale.z = forward.Length();

        if (_outScale.x == 0.0f) _outScale.x = kEpsilon;
        if (_outScale.y == 0.0f) _outScale.y = kEpsilon;
        if (_outScale.z == 0.0f) _outScale.z = kEpsilon;

        right /= _outScale.x;
        up /= _outScale.y;
        forward /= _outScale.z;

        if ((right ^ up).Dot(forward) < 0.0f)
        {
            right = -right;
            _outScale.x = -_outScale.x;
        }

        BUMatrix tmpMat(right, up, forward);
        tmpMat.ToQuat(_outRot);

        _outTrans.x = m[3][0];
        _outTrans.y = m[3][1];
        _outTrans.z = m[3][2];
    }

    void FromRows3(const BUVector3& _right, const BUVector3& _up, const BUVector3& _forward)
    {
        m[0][0] = _right.x;   m[0][1] = _right.y;   m[0][2] = _right.z;
        m[1][0] = _up.x;      m[1][1] = _up.y;      m[1][2] = _up.z;
        m[2][0] = _forward.x; m[2][1] = _forward.y; m[2][2] = _forward.z;
    }

    void InvertOrthoNormal()
    {
        BUMatrix r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[j][i];
        const BUVector3 t(m[3][0], m[3][1], m[3][2]);
        r.m[3][0] = -(t.x * r.m[0][0] + t.y * r.m[1][0] + t.z * r.m[2][0]);
        r.m[3][1] = -(t.x * r.m[0][1] + t.y * r.m[1][1] + t.z * r.m[2][1]);
        r.m[3][2] = -(t.x * r.m[0][2] + t.y * r.m[1][2] + t.z * r.m[2][2]);
        *this = r;
    }

    BUVector3 MultiplyMat3Vec3(const BUVector3& v) const
    {
        return BUVector3(v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0],
                         v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
                         v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2]);
    }
};
