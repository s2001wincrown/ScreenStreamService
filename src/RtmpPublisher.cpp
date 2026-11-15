// RtmpPublisher.cpp
#include "RtmpPublisher.h"
#include <cstring>

bool RtmpPublisher::Connect(const std::string& url,
                             const std::string& user,
                             const std::string& pass) {
    rtmp_ = RTMP_Alloc();
    if (!rtmp_) return false;
    RTMP_Init(rtmp_);

    std::string fullUrl = url;
    if (!user.empty()) fullUrl += " user=" + user;
    if (!pass.empty()) fullUrl += " pass=" + pass;

    if (!RTMP_SetupURL(rtmp_, const_cast<char*>(fullUrl.c_str()))) { Close(); return false; }
    RTMP_EnableWrite(rtmp_);
    if (!RTMP_Connect(rtmp_, nullptr)) { Close(); return false; }
    if (!RTMP_ConnectStream(rtmp_, 0)) { Close(); return false; }

    // 发送 FLV Header
    uint8_t flvHeader[] = {
        'F', 'L', 'V',
        0x01,       // version 1
        0x01,       // video only (no audio)
        0x00, 0x00, 0x00, 0x09, // header size
        0x00, 0x00, 0x00, 0x00  // previous tag size = 0
    };
    if (RTMP_Write(rtmp_, reinterpret_cast<char*>(flvHeader), sizeof(flvHeader)) !=
        static_cast<int>(sizeof(flvHeader))) {
        Close();
        return false;
    }

    return true;
}

bool RtmpPublisher::Send(const std::vector<uint8_t>& flvTag) {
    if (!rtmp_ || flvTag.empty()) return false;
    return RTMP_Write(rtmp_,
        reinterpret_cast<char*>(const_cast<uint8_t*>(flvTag.data())),
        static_cast<int>(flvTag.size())) == static_cast<int>(flvTag.size());
}

void RtmpPublisher::Close() {
    if (rtmp_) { RTMP_Close(rtmp_); RTMP_Free(rtmp_); rtmp_ = nullptr; }
}