// FlvMuxer.cpp
#include "FlvMuxer.h"
#include <cstring>
#include <algorithm>
#include <string>

void FlvMuxer::Open() {
    timestamp_ = 0;
    hasSeqHeader_ = false;
    sps_.clear();
    pps_.clear();
}

// 解析 Annex-B NALU，提取 SPS/PPS，返回 NALU 列表
static std::vector<std::vector<uint8_t>> ParseAnnexB(const std::vector<uint8_t>& data) {
    std::vector<std::vector<uint8_t>> nalus;
    size_t i = 0;
    while (i < data.size()) {
        // 查找 start code: 00 00 00 01 或 00 00 01
        size_t start = std::string::npos;
        if (i + 4 <= data.size() && data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1) {
            start = i + 4;
        } else if (i + 3 <= data.size() && data[i] == 0 && data[i+1] == 0 && data[i+2] == 1) {
            start = i + 3;
        }
        if (start == std::string::npos) { ++i; continue; }

        // 找下一个 start code
        size_t end = data.size();
        for (size_t j = start; j + 3 <= data.size(); ++j) {
            if ((data[j] == 0 && data[j+1] == 0 && data[j+2] == 1) ||
                (j + 4 <= data.size() && data[j] == 0 && data[j+1] == 0 && data[j+2] == 0 && data[j+3] == 1)) {
                end = j;
                break;
            }
        }

        std::vector<uint8_t> nalu(data.begin() + start, data.begin() + end);
        if (!nalu.empty()) nalus.push_back(std::move(nalu));
        i = end;
    }
    return nalus;
}

static uint8_t GetNaluType(const std::vector<uint8_t>& nalu) {
    return nalu.empty() ? 0 : nalu[0] & 0x1F;
}

static bool IsKeyframe(uint8_t naluType) {
    return naluType == 5; // IDR
}

std::vector<FlvPacket> FlvMuxer::WriteVideo(const std::vector<uint8_t>& annexb) {
    std::vector<FlvPacket> packets;
    auto nalus = ParseAnnexB(annexb);

    for (auto& nalu : nalus) {
        uint8_t type = GetNaluType(nalu);
        if (type == 7) { // SPS
            sps_ = nalu;
        } else if (type == 8) { // PPS
            pps_ = nalu;
        }
    }

    // 生成 sequence header
    if (!hasSeqHeader_ && !sps_.empty() && !pps_.empty()) {
        auto seqBody = BuildAvcSequenceHeader();
        auto tag = BuildVideoTag(seqBody, 0, true);
        packets.push_back({tag, 0});
        hasSeqHeader_ = true;
    }

    // 封装每个 NALU（实际可合并为一帧）
    for (auto& nalu : nalus) {
        uint8_t type = GetNaluType(nalu);
        if (type == 7 || type == 8) continue; // SPS/PPS 已处理

        bool keyframe = IsKeyframe(type);
        auto body = BuildAvcNaluPacket(nalu);
        auto tag = BuildVideoTag(body, timestamp_, keyframe);
        packets.push_back({tag, timestamp_});
    }

    timestamp_ += 33; // ~30fps
    return packets;
}

std::vector<uint8_t> FlvMuxer::BuildAvcSequenceHeader() {
    // AVCC extradata
    std::vector<uint8_t> body;
    body.push_back(1);  // configurationVersion
    body.push_back(sps_[1]); // AVCProfileIndication
    body.push_back(sps_[2]); // profile_compatibility
    body.push_back(sps_[3]); // AVCLevelIndication
    body.push_back(0xFF); // lengthSizeMinusOne = 3 (4 bytes)
    body.push_back(0xE1); // numOfSequenceParameterSets = 1

    // SPS
    uint16_t spsLen = (uint16_t)sps_.size();
    body.push_back((spsLen >> 8) & 0xFF);
    body.push_back(spsLen & 0xFF);
    body.insert(body.end(), sps_.begin(), sps_.end());

    // PPS
    body.push_back(1); // numOfPictureParameterSets = 1
    uint16_t ppsLen = (uint16_t)pps_.size();
    body.push_back((ppsLen >> 8) & 0xFF);
    body.push_back(ppsLen & 0xFF);
    body.insert(body.end(), pps_.begin(), pps_.end());

    return body;
}

std::vector<uint8_t> FlvMuxer::BuildAvcNaluPacket(const std::vector<uint8_t>& nalu) {
    std::vector<uint8_t> body;
    body.push_back(1); // AVCPacketType = 1 (NALU)
    body.push_back(0); // CompositionTime offset (3 bytes, big endian)
    body.push_back(0);
    body.push_back(0);

    // 4-byte NALU length (big endian)
    uint32_t len = (uint32_t)nalu.size();
    body.push_back((len >> 24) & 0xFF);
    body.push_back((len >> 16) & 0xFF);
    body.push_back((len >> 8) & 0xFF);
    body.push_back(len & 0xFF);
    body.insert(body.end(), nalu.begin(), nalu.end());

    return body;
}

std::vector<uint8_t> FlvMuxer::BuildVideoTag(const std::vector<uint8_t>& body, uint32_t ts, bool keyframe) {
    std::vector<uint8_t> tag;

    uint8_t frameType = keyframe ? 1 : 2;
    uint8_t codecId = 7; // AVC

    uint32_t bodySize = (uint32_t)(1 + body.size()); // FrameType+CodecID + body

    // Tag header (11 bytes)
    tag.push_back(0x09); // Video tag
    tag.push_back((bodySize >> 16) & 0xFF);
    tag.push_back((bodySize >> 8) & 0xFF);
    tag.push_back(bodySize & 0xFF);
    tag.push_back(ts & 0xFF);
    tag.push_back((ts >> 8) & 0xFF);
    tag.push_back((ts >> 16) & 0xFF);
    tag.push_back((ts >> 24) & 0xFF);
    tag.push_back(0); // streamId
    tag.push_back(0);
    tag.push_back(0);

    // Video tag body
    tag.push_back((frameType << 4) | codecId);
    tag.insert(tag.end(), body.begin(), body.end());

    // Previous tag size (4 bytes)
    uint32_t prevTagSize = 11 + bodySize;
    tag.push_back((prevTagSize >> 24) & 0xFF);
    tag.push_back((prevTagSize >> 16) & 0xFF);
    tag.push_back((prevTagSize >> 8) & 0xFF);
    tag.push_back(prevTagSize & 0xFF);

    return tag;
}