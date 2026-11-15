// QsvEncoder.h
#pragma once
#include "IEncoder.h"
#include <mfxvideo.h>
#include <string>

class QsvEncoder : public IEncoder {
public:
    bool Open(int w, int h, int fps, int bitrateBps, int gopSize) override;
    EncodedFrame Encode(ID3D11Texture2D* texture) override;
    void Close() override;
    const char* Name() const override { return "QSV"; }
private:
    mfxSession session_ = nullptr;
    mfxSyncPoint sync_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    mfxU16 alignedWidth_ = 0;
    mfxU16 alignedHeight_ = 0;
    mfxU16 alignedPitch_ = 0;
};
