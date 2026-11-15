// src/DxCapture.cpp
#include "DxCapture.h"
#include <Logger.h>

namespace {
void LogHResult(const wchar_t* operation, HRESULT result) {
    CLogger::LogError(operation, static_cast<DWORD>(result));
    CLogger::Message(0, L"%s HRESULT=0x%08X", operation,
        static_cast<unsigned int>(result));
}
}

DxCapture::DxCapture(int monitorIndex, int fps)
    : monitorIndex_(monitorIndex), fps_(fps) {
}

DxCapture::~DxCapture() {
    Stop();
}

bool DxCapture::Start() {
    if (device_ || context_ || duplication_)
        Stop();

    // 创建 D3D11 设备
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL usedLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, _countof(levels), D3D11_SDK_VERSION,
        &device_, &usedLevel, &context_);

    if (FAILED(hr)) {
        LogHResult(L"D3D11CreateDevice failed", hr);
        return false;
    }
    CLogger::Message(0, L"Created D3D11 device; feature level=0x%04X",
        static_cast<unsigned int>(usedLevel));

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) {
        LogHResult(L"Query IDXGIDevice failed", hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        LogHResult(L"IDXGIDevice::GetAdapter failed", hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(monitorIndex_, &output);
    if (FAILED(hr)) {
        CLogger::Message(0, L"IDXGIAdapter::EnumOutputs failed for monitor index %d",
            monitorIndex_);
        LogHResult(L"IDXGIAdapter::EnumOutputs failed", hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) {
        LogHResult(L"Query IDXGIOutput1 failed", hr);
        return false;
    }

    DXGI_OUTPUT_DESC desc{};
    hr = output->GetDesc(&desc);
    if (FAILED(hr)) {
        LogHResult(L"IDXGIOutput::GetDesc failed", hr);
        return false;
    }
    width_ = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    height_ = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    CLogger::Message(0, L"Selected output '%s': %dx%d, attached=%s",
        desc.DeviceName, width_, height_, desc.AttachedToDesktop ? L"yes" : L"no");

    hr = output1->DuplicateOutput(device_.Get(), &duplication_);
    if (FAILED(hr)) {
        LogHResult(L"IDXGIOutput1::DuplicateOutput failed", hr);
        return false;
    }

    CLogger::Message(0, L"Desktop duplication started: %dx%d", width_, height_);
    return true;
}

void DxCapture::Stop() {
    duplication_.Reset();
    lastTexture_.Reset();
    context_.Reset();
    device_.Reset();
}

bool DxCapture::Recover(HRESULT reason) {
    const ULONGLONG now = GetTickCount64();
    if (now - lastRecoveryAttempt_ < 5000)
        return false;

    lastRecoveryAttempt_ = now;
    LogHResult(L"Desktop duplication lost; recreating capture device", reason);
    Stop();
    if (!Start()) {
        CLogger::LogMessage(L"Desktop capture recovery failed; will retry shortly");
        return false;
    }

    CLogger::LogMessage(L"Desktop capture recovered successfully");
    return true;
}

ID3D11Texture2D* DxCapture::Capture() {
    if (!duplication_) {
        Recover(DXGI_ERROR_ACCESS_LOST);
        return nullptr;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    Microsoft::WRL::ComPtr<IDXGIResource> resource;

    HRESULT hr = duplication_->AcquireNextFrame(500, &frameInfo, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return lastTexture_.Get();
    if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_REMOVED ||
        hr == DXGI_ERROR_DEVICE_RESET) {
        Recover(hr);
        return nullptr;
    }
    if (FAILED(hr)) {
        static ULONGLONG lastLoggedAt = 0;
        const ULONGLONG now = GetTickCount64();
        if (lastLoggedAt == 0 || now - lastLoggedAt >= 5000) {
            lastLoggedAt = now;
            LogHResult(L"IDXGIOutputDuplication::AcquireNextFrame failed", hr);
        }
        return nullptr;
    }

    // 获取纹理
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    hr = resource.As(&texture);
    if (FAILED(hr)) {
        LogHResult(L"Query captured ID3D11Texture2D failed", hr);
        duplication_->ReleaseFrame();
        return nullptr;
    }

    // 复制到 staging 纹理以便编码器读取
    D3D11_TEXTURE2D_DESC texDesc{};
    texture->GetDesc(&texDesc);
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.BindFlags = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    texDesc.MiscFlags = 0;

    D3D11_TEXTURE2D_DESC currentDesc{};
    if (lastTexture_) lastTexture_->GetDesc(&currentDesc);
    if (!lastTexture_ || currentDesc.Width != texDesc.Width ||
        currentDesc.Height != texDesc.Height) {
        lastTexture_.Reset();
        hr = device_->CreateTexture2D(&texDesc, nullptr, &lastTexture_);
        if (FAILED(hr)) {
            LogHResult(L"ID3D11Device::CreateTexture2D staging texture failed", hr);
            duplication_->ReleaseFrame();
            return nullptr;
        }
    }

    context_->CopyResource(lastTexture_.Get(), texture.Get());
    // The staging copy is independent of the duplication surface. Release the
    // acquired frame before returning so consumers cannot block DXGI capture.
    duplication_->ReleaseFrame();
    return lastTexture_.Get();
}

// ============ 工厂函数实现 ============
ICapture* CreateCapture(int monitorIndex, int fps) {
    return new DxCapture(monitorIndex, fps);
}