// X264Encoder.cpp
#include "X264Encoder.h"
#include "DxgiConvert.h"
#include <vector>
#include <cstring>

bool X264Encoder::Open(int w, int h, int fps, int bitrateBps, int gopSize) {
    x264_param_default_preset(&param_, "ultrafast", "zerolatency");

    param_.i_threads      = 1;
    param_.i_width        = w;
    param_.i_height       = h;
    param_.i_fps_num      = fps;
    param_.i_fps_den      = 1;
    param_.i_keyint_max   = gopSize;
    param_.i_scenecut_threshold = 0;
    param_.rc.i_rc_method = X264_RC_ABR;
    param_.rc.i_bitrate   = bitrateBps / 1000;
    param_.rc.i_vbv_max_bitrate = bitrateBps / 1000;
    param_.rc.i_vbv_buffer_size = bitrateBps / 1000;
    param_.i_csp          = X264_CSP_NV12;

    encoder_ = x264_encoder_open(&param_);
    if (!encoder_) return false;

    x264_picture_alloc(&picIn_, X264_CSP_NV12, w, h);
    width_  = w;
    height_ = h;
    return true;
}

EncodedFrame X264Encoder::Encode(ID3D11Texture2D* texture) {
    EncodedFrame frame;
    if (!encoder_) return frame;

    // DXGI Texture(BGRA) → 系统内存 NV12
    std::vector<uint8_t> nv12 = DxgiConvert::ToNv12(texture, width_, height_);
    if (nv12.empty()) return frame;

    // 拷贝到 x264 picture
    int ySize  = width_ * height_;
    int uvSize = ySize / 2;
    for (int y = 0; y < height_; ++y)
        std::memcpy(picIn_.img.plane[0] + y * picIn_.img.i_stride[0],
                    nv12.data() + y * width_, width_);
    for (int y = 0; y < height_ / 2; ++y)
        std::memcpy(picIn_.img.plane[1] + y * picIn_.img.i_stride[1],
                    nv12.data() + ySize + y * width_, width_);
    picIn_.i_pts = frameIndex_++;

    x264_nal_t* nals = nullptr;
    int iNals = 0;
    x264_picture_t picOut{};

    if (x264_encoder_encode(encoder_, &nals, &iNals, &picIn_, &picOut) <= 0)
        return frame;

    // 拼接 Annex-B NALU
    for (int i = 0; i < iNals; ++i) {
        // Annex-B start code
        const uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};
        frame.annexb.insert(frame.annexb.end(), startCode, startCode + 4);
        frame.annexb.insert(frame.annexb.end(),
                            nals[i].p_payload,
                            nals[i].p_payload + nals[i].i_payload);
    }
    frame.pts = (picOut.i_pts * 1000LL) / param_.i_fps_num;
    frame.duration = 1000LL / param_.i_fps_num;
    frame.keyframe = picOut.b_keyframe != 0;
    return frame;
}

void X264Encoder::Close() {
    if (encoder_) {
        x264_encoder_close(encoder_);
        encoder_ = nullptr;
    }
    x264_picture_clean(&picIn_);
}