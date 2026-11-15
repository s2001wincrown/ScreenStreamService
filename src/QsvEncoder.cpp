// QsvEncoder.cpp
#include "QsvEncoder.h"
#include "DxgiConvert.h"
#include <Logger.h>
#include <vector>
#include <cstring>
#include <algorithm>

namespace {
void LogMfxStatus(const wchar_t* operation, mfxStatus status) {
    CLogger::Message(0, L"%s: Media SDK status=%d (0x%08X)", operation,
        static_cast<int>(status), static_cast<unsigned int>(status));
}
}

bool QsvEncoder::Open(int w, int h, int fps, int bitrateBps, int gopSize) {
    if (w <= 0 || h <= 0 || w > 65519 || h > 65519 || fps <= 0 ||
        bitrateBps <= 0 || gopSize <= 0) {
        CLogger::Message(0, L"QSV: invalid encoder settings %dx%d fps=%d bitrate=%d gop=%d",
            w, h, fps, bitrateBps, gopSize);
        return false;
    }

    // Initialize the legacy Media SDK dispatcher session.
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
    mfxVersion ver{};
    mfxStatus sts = MFXInit(impl, &ver, &session_);
    if (sts < MFX_ERR_NONE || !session_) {
        LogMfxStatus(L"QSV: MFXInit failed (check the Intel graphics driver and Media SDK runtime)", sts);
        if (session_) {
            MFXClose(session_);
            session_ = nullptr;
        }
        return false;
    }
    if (sts > MFX_ERR_NONE)
        LogMfxStatus(L"QSV: MFXInit returned a warning", sts);

    const mfxStatus versionStatus = MFXQueryVersion(session_, &ver);
    if (versionStatus != MFX_ERR_NONE)
        LogMfxStatus(L"QSV: MFXQueryVersion failed", versionStatus);

    mfxIMPL selectedImpl = MFX_IMPL_AUTO_ANY;
    const mfxStatus implStatus = MFXQueryIMPL(session_, &selectedImpl);
    if (implStatus == MFX_ERR_NONE) {
        const mfxIMPL baseImpl = MFX_IMPL_BASETYPE(selectedImpl);
        const wchar_t* implName = baseImpl == MFX_IMPL_HARDWARE ||
            baseImpl == MFX_IMPL_HARDWARE2 || baseImpl == MFX_IMPL_HARDWARE3 ||
            baseImpl == MFX_IMPL_HARDWARE4 ? L"hardware" :
            baseImpl == MFX_IMPL_SOFTWARE ? L"software" : L"unknown";
        CLogger::Message(0, L"QSV: Media SDK runtime version %u.%u, implementation=%s (0x%X)",
            ver.Major, ver.Minor, implName, static_cast<unsigned int>(selectedImpl));
    } else {
        CLogger::Message(0, L"QSV: Media SDK runtime version %u.%u",
            ver.Major, ver.Minor);
        LogMfxStatus(L"QSV: MFXQueryIMPL failed", implStatus);
    }

    const mfxU16 alignedWidth = static_cast<mfxU16>((w + 15) & ~15);
    const mfxU16 alignedHeight = static_cast<mfxU16>((h + 15) & ~15);
    const mfxU16 alignedPitch = static_cast<mfxU16>((w + 15) & ~15);
    mfxVideoParam param{};
    param.mfx.CodecId       = MFX_CODEC_AVC;
    param.mfx.TargetUsage   = MFX_TARGETUSAGE_BALANCED;
    param.mfx.RateControlMethod = MFX_RATECONTROL_VBR;
    param.mfx.TargetKbps    = bitrateBps / 1000;
    param.mfx.MaxKbps       = bitrateBps / 1000;
    param.mfx.FrameInfo.FourCC       = MFX_FOURCC_NV12;
    param.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    param.mfx.FrameInfo.Width        = alignedWidth;
    param.mfx.FrameInfo.Height       = alignedHeight;
    param.mfx.FrameInfo.CropW        = static_cast<mfxU16>(w);
    param.mfx.FrameInfo.CropH        = static_cast<mfxU16>(h);
    param.mfx.FrameInfo.FrameRateExtN = (mfxU16)fps;
    param.mfx.FrameInfo.FrameRateExtD = 1;
    param.mfx.GopPicSize      = (mfxU16)gopSize;
    param.mfx.GopRefDist      = 1;
    param.IOPattern           = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

    sts = MFXVideoENCODE_Init(session_, &param);
    if (sts < MFX_ERR_NONE) {
        LogMfxStatus(L"QSV: MFXVideoENCODE_Init failed", sts);
        MFXClose(session_);
        session_ = nullptr;
        return false;
    }
    if (sts > MFX_ERR_NONE)
        LogMfxStatus(L"QSV: MFXVideoENCODE_Init returned a warning", sts);

    width_  = w;
    height_ = h;
    alignedWidth_ = alignedWidth;
    alignedHeight_ = alignedHeight;
    alignedPitch_ = alignedPitch;
    return true;
}

EncodedFrame QsvEncoder::Encode(ID3D11Texture2D* texture) {
    EncodedFrame frame;
    if (!session_) return frame;

    // 1. DXGI Texture(BGRA) → 系统内存 NV12
    std::vector<uint8_t> converted = DxgiConvert::ToNv12(texture, width_, height_);
    if (converted.empty()) return frame;

    const size_t ySize = static_cast<size_t>(alignedPitch_) * alignedHeight_;
    std::vector<uint8_t> nv12(ySize + ySize / 2, 0);
    for (int row = 0; row < height_; ++row) {
        std::memcpy(nv12.data() + static_cast<size_t>(row) * alignedPitch_,
            converted.data() + static_cast<size_t>(row) * width_, width_);
    }
    const size_t sourceUvOffset = static_cast<size_t>(width_) * height_;
    const size_t destinationUvOffset = ySize;
    for (int row = 0; row < height_ / 2; ++row) {
        std::memcpy(nv12.data() + destinationUvOffset + static_cast<size_t>(row) * alignedPitch_,
            converted.data() + sourceUvOffset + static_cast<size_t>(row) * width_, width_);
    }

    // 2. Fill the aligned Media SDK surface while preserving the visible crop.
    mfxFrameSurface1 surface{};
    surface.Info.FourCC       = MFX_FOURCC_NV12;
    surface.Info.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    surface.Info.Width        = alignedWidth_;
    surface.Info.Height       = alignedHeight_;
    surface.Info.CropW        = static_cast<mfxU16>(width_);
    surface.Info.CropH        = static_cast<mfxU16>(height_);
    surface.Data.Pitch        = alignedPitch_;
    surface.Data.Y            = nv12.data();
    surface.Data.UV           = nv12.data() + ySize;

    // 3. 编码
    mfxBitstream bs{};
    const int bufSize = width_ * height_ * 3 / 2 + 1024 * 1024;
    std::vector<mfxU8> buffer(bufSize);
    bs.Data      = buffer.data();
    bs.MaxLength = bufSize;

    mfxEncodeCtrl ctrl{};
    mfxStatus sts = MFXVideoENCODE_EncodeFrameAsync(session_, &ctrl, &surface, &bs, &sync_);
    if (sts == MFX_WRN_DEVICE_BUSY || sts < MFX_ERR_NONE) return frame;

    if (sync_) {
        MFXVideoCORE_SyncOperation(session_, sync_, 1000);
        sync_ = nullptr;
    }

    if (bs.DataLength > 0) {
        frame.annexb.assign(bs.Data + bs.DataOffset,
                            bs.Data + bs.DataOffset + bs.DataLength);
    }
    return frame;
}

void QsvEncoder::Close() {
    if (session_) {
        MFXVideoENCODE_Close(session_);
        MFXClose(session_);
        session_ = nullptr;
    }
}