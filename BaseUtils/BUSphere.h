#pragma once
#include "BUMath.h"
#include "BUAABB.h"

class BUSphere
{
public:
    BUVector3 mOrigin;
    float mfRadius;

    bool IsPointInside(const BUVector3& _point) const
    {
        BUVector3 toPoint = _point - mOrigin;
        float distSq = toPoint.Dot(toPoint);
        return distSq < (mfRadius * mfRadius);
    }

    bool IsOverlapping(const BUVector3& _center, float _fRadius) const
    {
        BUVector3 dif = mOrigin - _center;
        float distSq = dif.Dot(dif);
        float radiusSum = mfRadius + _fRadius;
        return distSq < (radiusSum * radiusSum);
    }

    bool IsRayIntersect(BUVector3* _vRayStart, BUVector3* _vRay, float* _fHitDistance)
    {
        BUVector3 vRayNorm = *_vRay;
        float fRayLen = vRayNorm.Length();

        if (fRayLen > 0.0f) {
            vRayNorm = vRayNorm / fRayLen;
        }

        BUVector3 vDiff = *_vRayStart - mOrigin;

        float fB = BUVector3::Dot(vDiff, vRayNorm);
        float fC = BUVector3::Dot(vDiff, vDiff) - (mfRadius * mfRadius);

        float discriminant = fB * fB - fC;

        if (discriminant < 0.0f) {
            return false;
        }

        float sqrtDisc = sqrtf(discriminant);

        float t1 = -fB - sqrtDisc;
        float t2 = -fB + sqrtDisc;

        float t = (t1 >= 0.0f) ? t1 : t2;

        if (t >= 0.0f && t <= fRayLen) {
            *_fHitDistance = t;
            return true;
        }

        return false;
    }

    void AddEveryPoints(const BUVector3* _pPoints, uint32_t _uiNbPoints)
    {
        for (uint32_t i = 0; i < _uiNbPoints; ++i)
        {
            BUVector3 diff = _pPoints[i] - mOrigin;

            float distSq = diff.Dot(diff);

            if (mfRadius * mfRadius < distSq)
            {

                float dist = sqrtf(distSq);
                float oldRadius = mfRadius;

                mfRadius = 0.5f * (oldRadius + dist);

                diff = diff * (0.5f * (dist - oldRadius) / dist);
                mOrigin += diff;
            }
        }
    }

    bool Contain(const BUSphere& _sphere) const
    {
        float radiusDiff = mfRadius - _sphere.mfRadius;

        if (radiusDiff >= 0.0f)
        {
            BUVector3 diff = mOrigin - _sphere.mOrigin;
            return diff.Dot(diff) < (radiusDiff * radiusDiff);
        }

        return false;
    }

    bool ContainOrEqual(const BUSphere& _sphere) const
    {
        float radiusDiff = mfRadius - _sphere.mfRadius;

        if (radiusDiff >= 0.0f)
        {
            BUVector3 dif = mOrigin - _sphere.mOrigin;
            return dif.Dot(dif) <= (radiusDiff * radiusDiff);
        }

        return false;
    }

    void IncludePoint(const BUVector3& _point)
    {
        AddEveryPoints(&_point, 1);
    }

    bool IsCompletelyIncluded(const BUAABB& _aabb) const
    {
        float r = mfRadius;

        return (_aabb.mMin.x <= mOrigin.x - r) &&
            (mOrigin.x + r <= _aabb.mMax.x) &&
            (_aabb.mMin.y <= mOrigin.y - r) &&
            (mOrigin.y + r <= _aabb.mMax.y) &&
            (_aabb.mMin.z <= mOrigin.z - r) &&
            (mOrigin.z + r <= _aabb.mMax.z);
    }

    bool IsIntersectingCone(const BUVector3& _vPos, const BUVector3& _vAt, float _fRad, float _fLength) const
    {
        if (_fLength <= 0.0f) {
            return false;
        }

        BUVector3 vRelative = mOrigin - _vPos;
        float fProjectPos = BUVector3::Dot(vRelative, _vAt);

        if (fProjectPos > _fLength || fProjectPos < -mfRadius) {
            return false;
        }

        if (fProjectPos < 0.0f) {
            return (mOrigin - _vPos).LengthSquared() < (mfRadius * mfRadius);
        }

        float fConeSizeAtProj = (fProjectPos / _fLength) * _fRad;
        float fDistToAxisSq = (vRelative - _vAt * fProjectPos).LengthSquared();

        return fDistToAxisSq < (mfRadius + fConeSizeAtProj) * (mfRadius + fConeSizeAtProj);
    }

    bool IsOverlapping(const BUSphere& _sphere) const
    {
        return IsOverlapping(_sphere.mOrigin, _sphere.mfRadius);
    }

    void MostSeparatedPointsOnAABB(uint32_t* _uiMin, uint32_t* _uiMax, const BUVector3* _pPoints, uint32_t _uiNbPoints)
    {
        if (_uiNbPoints == 0) {
            *_uiMin = 0;
            *_uiMax = 0;
            return;
        }

        uint32_t iMinX = 0, iMaxX = 0;
        uint32_t iMinY = 0, iMaxY = 0;
        uint32_t iMinZ = 0, iMaxZ = 0;

        for (uint32_t i = 1; i < _uiNbPoints; ++i)
        {
            if (_pPoints[i].x < _pPoints[iMinX].x) iMinX = i;
            if (_pPoints[i].x > _pPoints[iMaxX].x) iMaxX = i;
            if (_pPoints[i].y < _pPoints[iMinY].y) iMinY = i;
            if (_pPoints[i].y > _pPoints[iMaxY].y) iMaxY = i;
            if (_pPoints[i].z < _pPoints[iMinZ].z) iMinZ = i;
            if (_pPoints[i].z > _pPoints[iMaxZ].z) iMaxZ = i;
        }

        float distSqX = (_pPoints[iMaxX] - _pPoints[iMinX]).LengthSquared();
        float distSqY = (_pPoints[iMaxY] - _pPoints[iMinY]).LengthSquared();
        float distSqZ = (_pPoints[iMaxZ] - _pPoints[iMinZ]).LengthSquared();

        if (distSqX >= distSqY && distSqX >= distSqZ)
        {
            *_uiMin = iMinX;
            *_uiMax = iMaxX;
        }
        else if (distSqY >= distSqZ)
        {
            *_uiMin = iMinY;
            *_uiMax = iMaxY;
        }
        else
        {
            *_uiMin = iMinZ;
            *_uiMax = iMaxZ;
        }
    }

    void SphereFromDistantPoints(const BUVector3* _pPoints, uint32_t _uiNbPoints)
    {
        if (_uiNbPoints == 0) {
            mOrigin.Set(0.0f);
            mfRadius = 0.0f;
            return;
        }

        uint32_t iMin = 0, iMax = 0;
        MostSeparatedPointsOnAABB(&iMin, &iMax, _pPoints, _uiNbPoints);

        mOrigin = (_pPoints[iMin] + _pPoints[iMax]) * 0.5f;

        BUVector3 diff = _pPoints[iMax] - _pPoints[iMin];
        mfRadius = diff.Length() * 0.5f;
    }
};
