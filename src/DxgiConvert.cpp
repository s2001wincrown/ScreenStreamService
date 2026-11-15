// DxgiConvert.cpp
#include "DxgiConvert.h"
#include <wrl/client.h>
#include <d3d11.h>
#include <vector>
#include <cstdint>
#include <algorithm>

std::vector<uint8_t> DxgiConvert::ToNv12(ID3D11Texture2D* texture, int w, int h) {
    if (!texture) return {};

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    texture->GetDevice(&device);
    if (!device) return {};

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    // 创建 staging texture
    desc.Usage          = D3D11_USAGE_STAGING;
    desc.BindFlags      = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return {};

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    device->GetImmediateContext(&ctx);
    ctx->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return {};

    // BGRA → NV12 转换（简化版，生产环境可用 libyuv / swscale）
    int ySize = w * h;
    std::vector<uint8_t> nv12(ySize + ySize / 2);

    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t* yDst = nv12.data();
    uint8_t* uvDst = nv12.data() + ySize;

    for (int y = 0; y < h; ++y) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(src + y * mapped.RowPitch);
        for (int x = 0; x < w; ++x) {
            uint32_t pixel = row[x];
            uint8_t b = pixel & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t r = (pixel >> 16) & 0xFF;

            int yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            yDst[y * w + x] = static_cast<uint8_t>(std::clamp(yy, 0, 255));

            if ((y & 1) == 0 && (x & 1) == 0) {
                int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                int uvIdx = (y / 2) * w + x;
                // NV12 stores interleaved U,V, not V,U.
                uvDst[uvIdx]     = static_cast<uint8_t>(std::clamp(u, 0, 255));
                uvDst[uvIdx + 1] = static_cast<uint8_t>(std::clamp(v, 0, 255));
            }
        }
    }

    ctx->Unmap(staging.Get(), 0);
    return nv12;
}