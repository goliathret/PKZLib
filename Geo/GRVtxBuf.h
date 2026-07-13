#pragma once
#include <cstdint>

class GRVtxBufSetup
{
public:
    int32_t iVertexFormat;
    uint8_t* pBuffer;
    uint32_t uiStride;
    uint8_t* pPositions;
    uint32_t uiPosStride;
    uint8_t* pNormals;
    uint32_t uiNormStride;
    uint8_t* pCustomUV;
    uint32_t uiCustomUVStride;
    uint8_t uiCustomUVIdx;
    uint8_t uiPad[3];

    GRVtxBufSetup()
        : iVertexFormat(0),
          pBuffer(0),
          uiStride(0xFFFFFFFF),
          pPositions(0),
          uiPosStride(0xFFFFFFFF),
          pNormals(0),
          uiNormStride(0xFFFFFFFF),
          pCustomUV(0),
          uiCustomUVStride(0xFFFFFFFF),
          uiCustomUVIdx(0xFF)
    {
        // Initialize padding to zero
        uiPad[0] = 0;
        uiPad[1] = 0;
        uiPad[2] = 0;
    }
};