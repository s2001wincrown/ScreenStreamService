// include/ICapture.h
#pragma once
#include <d3d11.h>
#include <wrl/client.h>

class ICapture {
public:
    virtual ~ICapture() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    // Returns a staging texture whose lifetime is owned by the capture object.
    // The texture is valid until the next Capture call or Stop.
    virtual ID3D11Texture2D* Capture() = 0;
    virtual int Width() const = 0;
    virtual int Height() const = 0;
};

// 工厂函数，在 DxCapture.cpp 中实现
ICapture* CreateCapture(int monitorIndex, int fps);