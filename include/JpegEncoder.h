// JpegEncoder.h
#pragma once
#include <d3d11.h>
#include <vector>
#include <cstdint>

struct JpegEncoder {
    // 从 D3D11 Texture 读取 → 缩放到目标尺寸 → 编码 JPEG
    // quality: 1~100
    static std::vector<uint8_t> Encode(ID3D11Texture2D* texture,
                                       int srcW, int srcH,
                                       int dstW, int dstH,
                                       int quality);
};