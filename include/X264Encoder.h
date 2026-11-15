// X264Encoder.h
#pragma once
#include "IEncoder.h"
#include <x264.h>
#include <string>

class X264Encoder : public IEncoder {
public:
    bool Open(int w, int h, int fps, int bitrateBps, int gopSize) override;
    EncodedFrame Encode(ID3D11Texture2D* texture) override;
    void Close() override;
    const char* Name() const override { return "x264"; }
    int width_ = 0;
    int height_ = 0;
    x264_param_t param_{};
private:
    x264_t* encoder_ = nullptr;
    x264_picture_t picIn_{};
    int64_t frameIndex_ = 0;
};