#pragma once
#include <algorithm>

template<typename T>
inline T BUMin(T a, T b)
{
    return (a < b) ? a : b;
}

template<typename T>
inline T BUMax(T a, T b)
{
    return (a > b) ? a : b;
}

class BUVector2
{
public:
    float x;
    float y;

    BUVector2() : x(0.0f), y(0.0f) {}

    BUVector2(float _x, float _y) : x(_x), y(_y) {}

    void Set(float _x, float _y)
    {
        x = _x;
        y = _y;
    }

    void Set(float _fVal)
    {
        x = _fVal;
        y = _fVal;
    }

    void Set(const BUVector2& _vector)
    {
        x = _vector.x;
        y = _vector.y;
    }

    float Length() const
    {
        return sqrtf(x*x + y*y);
    }

    BUVector2& operator+=(const BUVector2& rhs)
    {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }

    BUVector2& operator-=(const BUVector2& rhs)
    {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }

    BUVector2& operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    BUVector2& operator/=(float scalar)
    {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    friend BUVector2 operator+(BUVector2 lhs, const BUVector2& rhs)
    {
        lhs += rhs;
        return lhs;
    }

    friend BUVector2 operator-(BUVector2 lhs, const BUVector2& rhs)
    {
        lhs -= rhs;
        return lhs;
    }

    friend BUVector2 operator*(BUVector2 lhs, float scalar)
    {
        lhs *= scalar;
        return lhs;
    }

    friend BUVector2 operator*(float scalar, BUVector2 rhs)
    {
        rhs *= scalar;
        return rhs;
    }

    friend BUVector2 operator/(BUVector2 lhs, float scalar)
    {
        lhs /= scalar;
        return lhs;
    }

    BUVector2 operator-() const
    {
        return BUVector2{ -x, -y };
    }

    bool operator==(const BUVector2& rhs) const
    {
        return x == rhs.x && y == rhs.y;
    }

    bool operator!=(const BUVector2& rhs) const
    {
        return !(*this == rhs);
    }
};

class BUVector3
{
public:
    float x;
    float y;
    float z;

    BUVector3() : x(0.0f), y(0.0f), z(0.0) {}

    BUVector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    void Set(float _x, float _y, float _z)
    {
        x = _x;
        y = _y;
        z = _z;
    }
    
    void Set(float _fVal)
    {
        x = _fVal;
        y = _fVal;
        z = _fVal;
    }

    void Set(const BUVector3& _vector)
    {
        x = _vector.x;
        y = _vector.y;
        z = _vector.z;
    }

    float Length() const
    {
        return sqrtf(x*x + y*y + z*z);
    }

    float LengthSquared() const
    {
        return x * x + y * y + z * z;
    }

    BUVector3& operator+=(const BUVector3& rhs)
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
    BUVector3& operator-=(const BUVector3& rhs)
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }
    BUVector3& operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }
    BUVector3& operator/=(float scalar)
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    friend BUVector3 operator+(BUVector3 lhs, const BUVector3& rhs)
    {
        lhs += rhs;
        return lhs;
    }
    friend BUVector3 operator-(BUVector3 lhs, const BUVector3& rhs)
    {
        lhs -= rhs;
        return lhs;
    }
    friend BUVector3 operator*(BUVector3 lhs, float scalar)
    {
        lhs *= scalar;
        return lhs;
    }
    friend BUVector3 operator*(float scalar, BUVector3 rhs)
    {
        rhs *= scalar;
        return rhs;
    }
    friend BUVector3 operator/(BUVector3 lhs, float scalar)
    {
        lhs /= scalar;
        return lhs;
    }

    BUVector3 operator-() const
    {
        return BUVector3{ -x, -y, -z };
    }

    bool operator==(const BUVector3& rhs) const
    {
        return x == rhs.x && y == rhs.y && z == rhs.z;
    }
    bool operator!=(const BUVector3& rhs) const
    {
        return !(*this == rhs);
    }

    float Dot(const BUVector3& rhs) const
    {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }

    static float Dot(const BUVector3& a, const BUVector3& b)
    {
        return a.Dot(b);
    }

    void Min(const BUVector3& rhs)
    {
        x = std::min(x, rhs.x);
        y = std::min(y, rhs.y);
        z = std::min(z, rhs.z);
    }
    void Max(const BUVector3& rhs)
    {
        x = std::max(x, rhs.x);
        y = std::max(y, rhs.y);
        z = std::max(z, rhs.z);
    }
};

class BUVector4
{
public:
    float x;
    float y;
    float z;
    float w;

    BUVector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}

    BUVector4(float _x, float _y, float _z, float _w)
        : x(_x), y(_y), z(_z), w(_w) {}

    void Set(float _x, float _y, float _z, float _w)
    {
        x = _x;
        y = _y;
        z = _z;
        w = _w;
    }

    void Set(float _fVal)
    {
        x = _fVal;
        y = _fVal;
        z = _fVal;
        w = _fVal;
    }

    void Set(const BUVector4& _vector)
    {
        x = _vector.x;
        y = _vector.y;
        z = _vector.z;
        w = _vector.w;
    }

    float Length() const
    {
        return sqrtf(x*x + y*y + z*z + w*w);
    }

    BUVector4& operator+=(const BUVector4& rhs)
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        w += rhs.w;
        return *this;
    }

    BUVector4& operator-=(const BUVector4& rhs)
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        w -= rhs.w;
        return *this;
    }

    BUVector4& operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    BUVector4& operator/=(float scalar)
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }

    friend BUVector4 operator+(BUVector4 lhs, const BUVector4& rhs)
    {
        lhs += rhs;
        return lhs;
    }

    friend BUVector4 operator-(BUVector4 lhs, const BUVector4& rhs)
    {
        lhs -= rhs;
        return lhs;
    }

    friend BUVector4 operator*(BUVector4 lhs, float scalar)
    {
        lhs *= scalar;
        return lhs;
    }

    friend BUVector4 operator*(float scalar, BUVector4 rhs)
    {
        rhs *= scalar;
        return rhs;
    }

    friend BUVector4 operator/(BUVector4 lhs, float scalar)
    {
        lhs /= scalar;
        return lhs;
    }

    BUVector4 operator-() const
    {
        return BUVector4{ -x, -y, -z, -w };
    }

    bool operator==(const BUVector4& rhs) const
    {
        return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
    }

    bool operator!=(const BUVector4& rhs) const
    {
        return !(*this == rhs);
    }
};

class BUQuaternion
{
public:
    float x;
    float y;
    float z;
    float w;

    void Conjugate()
    {
        x = -x;
        y = -y;
        z = -z;
    }

    void Set(float _x, float _y, float _z, float _w)
    {
        x = _x;
        y = _y;
        z = _z;
        w = _w;
    }

    void SetMulIdentity()
    {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
        w = 1.0f;
    }

    void Rotate(BUVector3& _dest, const BUVector3& _axis) const
    {
        BUQuaternion pure;
        pure.Set(_axis.x, _axis.y, _axis.z, 0.0f);
        BUQuaternion inverse;
        inverse.Set(-x, -y, -z, w);

        BUQuaternion result = (*this * pure) * inverse;

        _dest.x = result.x;
        _dest.y = result.y;
        _dest.z = result.z;
    }

    BUQuaternion& operator+=(const BUQuaternion& rhs)
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        w += rhs.w;
        return *this;
    }
    BUQuaternion& operator-=(const BUQuaternion& rhs)
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        w -= rhs.w;
        return *this;
    }
    BUQuaternion& operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    BUQuaternion& operator*=(const BUQuaternion& rhs)
    {
        *this = BUQuaternion{
            w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
            w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
            w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
            w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z
        };
        return *this;
    }

    friend BUQuaternion operator+(BUQuaternion lhs, const BUQuaternion& rhs)
    {
        lhs += rhs;
        return lhs;
    }
    friend BUQuaternion operator-(BUQuaternion lhs, const BUQuaternion& rhs)
    {
        lhs -= rhs;
        return lhs;
    }
    friend BUQuaternion operator*(BUQuaternion lhs, float scalar)
    {
        lhs *= scalar;
        return lhs;
    }
    friend BUQuaternion operator*(float scalar, BUQuaternion rhs)
    {
        rhs *= scalar;
        return rhs;
    }
    friend BUQuaternion operator*(BUQuaternion lhs, const BUQuaternion& rhs)
    {
        lhs *= rhs;
        return lhs;
    }

    BUQuaternion operator-() const
    {
        return BUQuaternion{ -x, -y, -z, -w };
    }

    bool operator==(const BUQuaternion& rhs) const
    {
        return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
    }
    bool operator!=(const BUQuaternion& rhs) const
    {
        return !(*this == rhs);
    }
};