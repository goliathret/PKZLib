#pragma once

#include "BUTruncate.h"
#include "BUMath.h"

class BURect
{
public:
    float x;
    float y;
    float w;
    float h;

    BURect() : x(0), y(0), w(0), h(0) {}
    BURect(float _x, float _y, float _w, float _h) 
        : x(_x), y(_y), w(_w), h(_h) {}

    bool GetIntersect(BURect *_rect)
    {
        float right = x + w;
        if (_rect->x + _rect->w < right) {
            right = _rect->x + _rect->w;
        }

        if (x < _rect->x) {
            x = _rect->x;
        }
        w = right - x;

        float bottom = y + h;
        if (_rect->y + _rect->h < bottom) {
            bottom = _rect->y + _rect->h;
        }

        if (y < _rect->y) {
            y = _rect->y;
        }
        h = bottom - y;

        return (w > 0.0f) && (h > 0.0f);
    }

    bool operator==(const BURect& _Rect) const
    {
        return (x == _Rect.x) && (y == _Rect.y) && 
            (w == _Rect.w) && (h == _Rect.h);
    }

    bool operator!=(const BURect& _Rect) const
    {
        return !(*this == _Rect);
    }

    void TruncToInteger()
    {
        x = BUTruncate::FtoF(x);
        y = BUTruncate::FtoF(y);
        w = BUTruncate::FtoF(w);
        h = BUTruncate::FtoF(h);
    }

    void UnionRect(const BURect& _r)
    {
        float minX = BUMin(x, _r.x);
        float minY = BUMin(y, _r.y);
        float maxX = BUMax(x + w, _r.x + _r.w);
        float maxY = BUMax(y + h, _r.y + _r.h);

        x = minX;
        y = minY;
        w = maxX - minX;
        h = maxY - minY;
    }
};