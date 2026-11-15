// src/DxCapture.h
#pragma once
#include "ICapture.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

class DxCapture : public ICapture {
public:
    DxCapture(int monitorIndex, int fps);
    ~DxCapture() override;

    bool Start() override;
    void Stop() override;
    ID3D11Texture2D* Capture() override;
    int Width() const override { return width_; }
    int Height() const override { return height_; }

private:
    bool Recover(HRESULT reason);

    int monitorIndex_;
    int fps_;
    int width_ = 0;
    int height_ = 0;
    ULONGLONG lastRecoveryAttempt_ = 0;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> lastTexture_;
};