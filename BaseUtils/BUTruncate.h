#pragma once

#include <cmath>

class BUTruncate
{
public:

    static inline float FtoF(float f)
    {
        return std::trunc(f);
    }
};
