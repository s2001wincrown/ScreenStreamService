// RtmpPublisher.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <rtmp.h>

class RtmpPublisher {
public:
    bool Connect(const std::string& url, const std::string& user, const std::string& pass);
    bool Send(const std::vector<uint8_t>& flvTag);
    void Close();

private:
    RTMP* rtmp_ = nullptr;
};