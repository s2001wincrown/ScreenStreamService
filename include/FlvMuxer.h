// FlvMuxer.h
#pragma once
#include <vector>
#include <cstdint>

struct FlvPacket {
    std::vector<uint8_t> data; // FLV tag header + body + prev tag size
    uint32_t timestamp;
};

class FlvMuxer {
public:
    void Open();
    // 输入 Annex-B NALU，输出 FLV 包
    std::vector<FlvPacket> WriteVideo(const std::vector<uint8_t>& annexb);

private:
    std::vector<uint8_t> sps_, pps_;
    bool hasSeqHeader_ = false;
    uint32_t timestamp_ = 0;

    std::vector<uint8_t> BuildAvcSequenceHeader();
    static std::vector<uint8_t> BuildAvcNaluPacket(const std::vector<uint8_t>& nalu);
    static std::vector<uint8_t> BuildVideoTag(const std::vector<uint8_t>& body, uint32_t ts, bool keyframe);
};