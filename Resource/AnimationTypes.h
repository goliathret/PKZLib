#pragma once
#include "../BaseUtils/BUMatrix.h"
#include <array>

using ANValue = std::array<float, 4>;

// Decoded transforms, not a binary overlay of the engine's runtime objects.
struct CMTransforms
{
    ANValue translation{0, 0, 0, 0};
    ANValue rotation{0, 0, 0, 1};
    ANValue scale{1, 1, 1, 0};
    BUMatrix Matrix() const;
    BUMatrix InverseMatrix() const;
};
