// include/IEncoder.h
#pragma once
#include <d3d11.h>
#include <vector>
#include <cstdint>
#include <string>

struct EncodedFrame {
    std::vector<uint8_t> annexb;  // Annex-B 格式 H.264
    int64_t pts = 0;              // 毫秒时间戳
    int64_t duration = 0;
    bool keyframe = false;
};

class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual bool Open(int width, int height, int fps, int bitrate, int gop) = 0;
    virtual void Close() = 0;
    virtual EncodedFrame Encode(ID3D11Texture2D* texture) = 0;
    virtual const char* Name() const = 0;
};
