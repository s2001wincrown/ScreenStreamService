// JpegEncoder.cpp
#include "JpegEncoder.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <Logger.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <limits>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "ole32.lib")

namespace {
void LogHResult(const wchar_t* operation, HRESULT result) {
    CLogger::LogError(operation, static_cast<DWORD>(result));
    CLogger::Message(0, L"%s HRESULT=0x%08X", operation,
        static_cast<unsigned int>(result));
}

class ComInitialization {
public:
    ComInitialization() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComInitialization() {
        if (SUCCEEDED(result_)) CoUninitialize();
    }
    HRESULT Result() const { return result_; }

private:
    HRESULT result_;
};
}

static std::vector<uint8_t> ReadTexture(ID3D11Texture2D* texture, int w, int h) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    texture->GetDevice(&device);
    if (!device) {
        CLogger::LogMessage(L"JPEG: captured texture has no D3D11 device");
        return {};
    }

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (w <= 0 || h <= 0 || desc.Width < static_cast<UINT>(w) ||
        desc.Height < static_cast<UINT>(h)) {
        CLogger::Message(0, L"JPEG: invalid source dimensions %dx%d (texture %ux%u)",
            w, h, desc.Width, desc.Height);
        return {};
    }

    desc.Usage          = D3D11_USAGE_STAGING;
    desc.BindFlags      = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) {
        LogHResult(L"JPEG: creating staging texture failed", hr);
        return {};
    }

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    device->GetImmediateContext(&ctx);
    if (!ctx) {
        CLogger::LogMessage(L"JPEG: unable to get D3D11 immediate context");
        return {};
    }
    ctx->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        LogHResult(L"JPEG: mapping staging texture failed", hr);
        return {};
    }

    const size_t rowSize = static_cast<size_t>(w) * 4;
    std::vector<uint8_t> bgra(rowSize * static_cast<size_t>(h));

    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < h; ++y) {
        std::memcpy(bgra.data() + static_cast<size_t>(y) * rowSize,
            src + static_cast<size_t>(y) * mapped.RowPitch, rowSize);
    }

    ctx->Unmap(staging.Get(), 0);
    return bgra;
}

static std::vector<uint8_t> ScaleAndConvert(const std::vector<uint8_t>& bgra,
                                            int srcW, int srcH,
                                            int dstW, int dstH) {
    std::vector<uint8_t> rgb(static_cast<size_t>(dstW) * dstH * 3);

    for (int dy = 0; dy < dstH; ++dy) {
        const int py = dy * srcH / dstH;
        for (int dx = 0; dx < dstW; ++dx) {
            const int px = dx * srcW / dstW;
            const size_t sourceIndex = (static_cast<size_t>(py) * srcW + px) * 4;
            const size_t destinationIndex = (static_cast<size_t>(dy) * dstW + dx) * 3;
            rgb[destinationIndex] = bgra[sourceIndex];
            rgb[destinationIndex + 1] = bgra[sourceIndex + 1];
            rgb[destinationIndex + 2] = bgra[sourceIndex + 2];
        }
    }
    return rgb;
}

std::vector<uint8_t> JpegEncoder::Encode(ID3D11Texture2D* texture,
                                         int srcW, int srcH,
                                         int dstW, int dstH,
                                         int quality) {
    if (!texture) return {};
    if (dstW <= 0 || dstH <= 0 || quality < 1 || quality > 100) {
        CLogger::Message(0, L"JPEG: invalid output settings %dx%d quality=%d",
            dstW, dstH, quality);
        return {};
    }

    ComInitialization com;
    if (FAILED(com.Result()) && com.Result() != RPC_E_CHANGED_MODE) {
        LogHResult(L"JPEG: COM initialization failed", com.Result());
        return {};
    }

    auto bgra = ReadTexture(texture, srcW, srcH);
    if (bgra.empty()) return {};

    auto rgb = ScaleAndConvert(bgra, srcW, srcH, dstW, dstH);

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        LogHResult(L"JPEG: creating WIC imaging factory failed", hr);
        return {};
    }

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    hr = factory->CreateBitmapFromMemory(dstW, dstH, GUID_WICPixelFormat24bppBGR,
        static_cast<UINT>(dstW * 3), static_cast<UINT>(rgb.size()), rgb.data(), &bitmap);
    if (FAILED(hr)) {
        LogHResult(L"JPEG: creating WIC bitmap failed", hr);
        return {};
    }

    Microsoft::WRL::ComPtr<IStream> memoryStream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &memoryStream);
    if (FAILED(hr)) {
        LogHResult(L"JPEG: creating seekable memory stream failed", hr);
        return {};
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromIStream(memoryStream.Get());
    if (FAILED(hr)) {
        LogHResult(L"JPEG: initializing WIC stream failed", hr);
        return {};
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        LogHResult(L"JPEG: initializing JPEG encoder failed", hr);
        return {};
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(static_cast<UINT>(dstW), static_cast<UINT>(dstH));
    if (FAILED(hr)) {
        LogHResult(L"JPEG: initializing bitmap frame failed", hr);
        return {};
    }

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) {
        LogHResult(L"JPEG: setting frame pixel format failed", hr);
        return {};
    }
    if (!IsEqualGUID(pixelFormat, GUID_WICPixelFormat24bppBGR)) {
        CLogger::Message(0, L"JPEG: encoder negotiated an unexpected pixel format (0x%08X)",
            static_cast<unsigned int>(pixelFormat.Data1));
        return {};
    }
    hr = frame->WriteSource(bitmap.Get(), nullptr);
    if (FAILED(hr)) {
        LogHResult(L"JPEG: writing frame pixels failed", hr);
        return {};
    }
    hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (FAILED(hr)) {
        LogHResult(L"JPEG: committing encoded frame failed", hr);
        return {};
    }

    STATSTG streamStatus{};
    hr = memoryStream->Stat(&streamStatus, STATFLAG_NONAME);
    if (FAILED(hr) || streamStatus.cbSize.QuadPart == 0 ||
        streamStatus.cbSize.QuadPart > (std::numeric_limits<size_t>::max)()) {
        LogHResult(L"JPEG: reading encoded stream size failed",
            FAILED(hr) ? hr : E_OUTOFMEMORY);
        return {};
    }

    std::vector<uint8_t> jpeg(static_cast<size_t>(streamStatus.cbSize.QuadPart));
    LARGE_INTEGER start{};
    hr = memoryStream->Seek(start, STREAM_SEEK_SET, nullptr);
    ULONG bytesRead = 0;
    if (SUCCEEDED(hr)) hr = memoryStream->Read(jpeg.data(),
        static_cast<ULONG>(jpeg.size()), &bytesRead);
    if (FAILED(hr) || bytesRead != jpeg.size()) {
        LogHResult(L"JPEG: reading encoded stream failed", FAILED(hr) ? hr : E_FAIL);
        return {};
    }
    return jpeg;
}
