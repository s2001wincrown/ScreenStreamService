// NvencEncoder.cpp
#include "NvencEncoder.h"
#include <stdexcept>
#include <cstring>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#define NVENC_THROW(call) do { \
    NVENCSTATUS st = (call); \
    if (st != NV_ENC_SUCCESS) \
        throw std::runtime_error("NVENC error: " + std::to_string((int)st)); \
} while(0)

struct NvencEncoder::Impl {
    NV_ENCODE_API_FUNCTION_LIST fn = { NV_ENCODE_API_FUNCTION_LIST_VER };
    void* encoder = nullptr;

    NV_ENC_REGISTERED_PTR registeredResource = nullptr;
    NV_ENC_INPUT_PTR inputBuffer = nullptr;
    NV_ENC_OUTPUT_PTR outputBuffer = nullptr;
    void* outputBitstream = nullptr;
    uint32_t outputBitstreamSize = 0;

    int width = 0, height = 0;
    ID3D11Device* device = nullptr;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11Texture2D> stagingTexture;
};

NvencEncoder::NvencEncoder() : pimpl_(std::make_unique<Impl>()) {}
NvencEncoder::~NvencEncoder() { Close(); }

bool NvencEncoder::Open(int w, int h, int fps, int bitrateBps, int gopSize) {
    ID3D11Device* device = nullptr;
    // NVENC is selected only when the capture device is supplied by the
    // service. The D3D11 device is attached through the input texture below.
    // This implementation requires the service to use the texture's device.
    (void)device;
    return false;
/*
    auto* p = pimpl_.get();
    p->device = device;
    p->width = w;
    p->height = h;

    device->GetImmediateContext(&p->ctx);

    NVENC_THROW(NvEncodeAPICreateInstance(&p->fn));

    // 打开编码会话
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session{};
    session.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    session.device = device;
    session.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    session.apiVersion = NVENCAPI_VERSION;
    NVENC_THROW(p->fn.nvEncOpenEncodeSessionEx(&session, &p->encoder));

    // 查询预设
    NV_ENC_PRESET_CONFIG presetCfg{};
    presetCfg.version = NV_ENC_PRESET_CONFIG_VER;
    presetCfg.presetCfg.version = NV_ENC_CONFIG_VER;
    NVENC_THROW(p->fn.nvEncGetEncodePresetConfigEx(
        p->encoder,
        NV_ENC_CODEC_H264_GUID,
        NV_ENC_PRESET_P1_GUID,   // 低延迟高性能
        NV_ENC_TUNING_INFO_LOW_LATENCY,
        &presetCfg));

    // 初始化编码器
    NV_ENC_INITIALIZE_PARAMS initParams{};
    initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
    initParams.presetGUID = NV_ENC_PRESET_P1_GUID;
    initParams.encodeWidth = w;
    initParams.encodeHeight = h;
    initParams.darWidth = w;
    initParams.darHeight = h;
    initParams.frameRateNum = fps;
    initParams.frameRateDen = 1;
    initParams.enablePTD = 1;
    initParams.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;

    NV_ENC_CONFIG* config = &presetCfg.presetCfg;
    config->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    config->rcParams.averageBitRate = bitrateBps;
    config->rcParams.maxBitRate = bitrateBps;
    config->gopLength = gopSize;
    config->frameIntervalP = 1; // 无 B 帧
    config->h264Config.idrPeriod = gopSize;

    initParams.encodeConfig = &presetCfg.presetCfg;
    NVENC_THROW(p->fn.nvEncInitializeEncoder(p->encoder, &initParams));

    // 注册 D3D11 纹理作为输入资源
    NV_ENC_REGISTER_RESOURCE res{};
    res.version = NV_ENC_REGISTER_RESOURCE_VER;
    res.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    res.resourceToRegister = nullptr; // 每帧动态注册
    res.width = w;
    res.height = h;
    res.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
    res.bufferUsage = NV_ENC_INPUT_IMAGE;
    // 注意：NVENC 允许每帧注册不同纹理，但更高效的是预先创建输入缓冲
    // 这里采用更简单的方式：创建 NVENC 输入缓冲，每帧 CopyResource 进去

    // 创建 NVENC 输入缓冲
    NV_ENC_CREATE_INPUT_BUFFER inBuf{};
    inBuf.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
    inBuf.width = w;
    inBuf.height = h;
    inBuf.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
    NVENC_THROW(p->fn.nvEncCreateInputBuffer(p->encoder, &inBuf));
    p->inputBuffer = inBuf.inputBuffer;

    // 创建输出比特流缓冲
    NV_ENC_CREATE_BITSTREAM_BUFFER outBuf{};
    outBuf.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    outBuf.size = w * h * 2; // 足够大
    NVENC_THROW(p->fn.nvEncCreateBitstreamBuffer(p->encoder, &outBuf));
    p->outputBuffer = outBuf.bitstreamBuffer;
    p->outputBitstreamSize = outBuf.size;

    // 创建 staging texture 用于 CopyResource
    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateTexture2D(&texDesc, nullptr, &p->stagingTexture);

    return true;*/
}

EncodedFrame NvencEncoder::Encode(ID3D11Texture2D* texture) {
    EncodedFrame frame;
    auto* p = pimpl_.get();
    if (!texture) return frame;

    // 1. 将桌面纹理拷贝到 staging texture
    p->ctx->CopyResource(p->stagingTexture.Get(), texture);

    // 2. 映射 NVENC 输入缓冲
    NV_ENC_LOCK_INPUT_BUFFER lockBuf{};
    lockBuf.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lockBuf.inputBuffer = p->inputBuffer;
    NVENC_THROW(p->fn.nvEncLockInputBuffer(p->encoder, &lockBuf));

    // 3. 从 staging texture 读取像素并拷贝到 NVENC 输入缓冲
    D3D11_MAPPED_SUBRESOURCE mapped{};
    p->ctx->Map(p->stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);

    uint8_t* src = static_cast<uint8_t*>(mapped.pData);
    uint8_t* dst = static_cast<uint8_t*>(lockBuf.bufferDataPtr);
    int pitch = p->width * 4; // ARGB = 4 bytes

    for (int y = 0; y < p->height; ++y) {
        memcpy(dst + y * lockBuf.pitch, src + y * mapped.RowPitch, pitch);
    }

    p->ctx->Unmap(p->stagingTexture.Get(), 0);
    NVENC_THROW(p->fn.nvEncUnlockInputBuffer(p->encoder, p->inputBuffer));

    // 4. 编码
    NV_ENC_PIC_PARAMS picParams{};
    picParams.version = NV_ENC_PIC_PARAMS_VER;
    picParams.inputBuffer = p->inputBuffer;
    picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
    picParams.inputWidth = p->width;
    picParams.inputHeight = p->height;
    picParams.outputBitstream = p->outputBuffer;
    picParams.inputTimeStamp = ++frameCount_;
    NVENC_THROW(p->fn.nvEncEncodePicture(p->encoder, &picParams));

    // 5. 锁定输出比特流
    NV_ENC_LOCK_BITSTREAM lockBitstream{};
    lockBitstream.version = NV_ENC_LOCK_BITSTREAM_VER;
    lockBitstream.outputBitstream = p->outputBuffer;
    NVENC_THROW(p->fn.nvEncLockBitstream(p->encoder, &lockBitstream));

    std::vector<uint8_t> annexb(
        static_cast<uint8_t*>(lockBitstream.bitstreamBufferPtr),
        static_cast<uint8_t*>(lockBitstream.bitstreamBufferPtr) + lockBitstream.bitstreamSizeInBytes);

    NVENC_THROW(p->fn.nvEncUnlockBitstream(p->encoder, p->outputBuffer));

    frame.annexb = std::move(annexb);
    frame.pts = frameCount_;
    frame.keyframe = false;
    return frame;
}

void NvencEncoder::Close() {
    auto* p = pimpl_.get();
    if (p->encoder) {
        // 刷新编码器
        NV_ENC_PIC_PARAMS flush{};
        flush.version = NV_ENC_PIC_PARAMS_VER;
        flush.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
        p->fn.nvEncEncodePicture(p->encoder, &flush);

        if (p->inputBuffer) {
            p->fn.nvEncDestroyInputBuffer(p->encoder, p->inputBuffer);
            p->inputBuffer = nullptr;
        }
        if (p->outputBuffer) {
            p->fn.nvEncDestroyBitstreamBuffer(p->encoder, p->outputBuffer);
            p->outputBuffer = nullptr;
        }
        p->fn.nvEncDestroyEncoder(p->encoder);
        p->encoder = nullptr;
    }
    p->ctx.Reset();
    p->stagingTexture.Reset();
}