// DxgiConvert.h
#pragma once
#include <d3d11.h>
#include <vector>
#include <cstdint>

struct DxgiConvert {
    static std::vector<uint8_t> ToNv12(ID3D11Texture2D* texture, int w, int h);
};