// NvencEncoder.h
#pragma once
#include <vector>
#include <cstdint>
#include <d3d11.h>
#include <memory>
#include <wrl/client.h>
#include "IEncoder.h"
#include "nvEncodeAPI.h"

class NvencEncoder : public IEncoder {
public:
    NvencEncoder();
    ~NvencEncoder() override;
    bool Open(int w, int h, int fps, int bitrateBps, int gopSize) override;
    EncodedFrame Encode(ID3D11Texture2D* texture) override;
    void Close() override;
    const char* Name() const override { return "NVENC"; }

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
    int64_t frameCount_ = 0;
};