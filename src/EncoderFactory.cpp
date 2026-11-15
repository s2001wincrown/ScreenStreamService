// EncoderFactory.cpp
#include "EncoderFactory.h"
#include "X264Encoder.h"
#if defined(SCREENSTREAM_ENABLE_QSV)
#include "QsvEncoder.h"
#endif
#include <dxgi.h>
#include <algorithm>
#include <string>
#include <Logger.h>
#include <wrl/client.h>

static bool HasNvidiaGPU() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return false;

    UINT i = 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    while (factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                ++i;
                continue;
            }
            std::wstring name(desc.Description);
            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
            if (name.find(L"nvidia") != std::wstring::npos)
                return true;
        }
        ++i;
    }
    return false;
}

static bool HasIntelGPU() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return false;

    UINT i = 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    while (factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                ++i;
                continue;
            }
            std::wstring name(desc.Description);
            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
            if (name.find(L"intel") != std::wstring::npos)
                return true;
        }
        ++i;
    }
    return false;
}

EncoderFactory::Type EncoderFactory::Detect() {
    if (HasNvidiaGPU()) return Type::NVENC;
    if (HasIntelGPU())  return Type::QSV;
    return Type::X264;
}

std::unique_ptr<IEncoder> EncoderFactory::Create(int width, int height, int fps,
                                                  int bitrateBps, int gopSize) {
    // NVENC needs the capture D3D11 device and is opened by the D3D-aware
    // composition root. QSV and x264 use the shared system-memory NV12 path.
#if defined(SCREENSTREAM_ENABLE_QSV)
    auto qsv = std::make_unique<QsvEncoder>();
    if (qsv->Open(width, height, fps, bitrateBps, gopSize)) {
        CLogger::LogMessage(L"Using Intel QSV encoder");
        return qsv;
    }
    CLogger::LogMessage(L"Intel QSV initialization failed; trying x264 fallback");
#endif

    auto x264 = std::make_unique<X264Encoder>();
    if (!x264->Open(width, height, fps, bitrateBps, gopSize)) {
        CLogger::LogMessage(L"x264 encoder initialization failed");
        return nullptr;
    }
    CLogger::LogMessage(L"Using x264 encoder");
    return x264;
}