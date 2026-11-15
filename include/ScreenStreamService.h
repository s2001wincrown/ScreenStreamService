// include/ScreenStreamService.h
#pragma once
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <string>
#include <mutex>

#include "ICapture.h"
#include "IEncoder.h"
#include "../include/FlvMuxer.h"
#include "../include/RtmpPublisher.h"
#include "HttpPreviewServer.h"

class ScreenStreamService {
public:
    ScreenStreamService();
    ~ScreenStreamService();

    bool Initialize(const std::filesystem::path& configPath = "Config.ini",
                    DWORD* errorCode = nullptr);
    bool Start(DWORD* errorCode = nullptr);
    void Stop();

    bool IsRunning() const { return running_.load(); }
    bool IsRtmpEnabled() const { return rtmpEnabled_; }
    bool IsPreviewEnabled() const { return previewEnabled_; }

private:
    void StreamLoop();
    std::vector<uint8_t> GrabPreviewFrame();

    // 配置
    bool rtmpEnabled_ = false;
    bool previewEnabled_ = false;

    // 主链路参数
    int width_ = 1920;
    int height_ = 1080;
    int fps_ = 30;
    int bitrate_ = 4000;
    int gop_ = 60;
    std::string rtmpUrl_;

    // 预览参数
    int previewPort_ = 8888;
    int previewWidth_ = 640;
    int previewHeight_ = 360;
    int previewQuality_ = 70;
    int previewMaxFps_ = 15;

    // 采集参数
    int captureMonitor_ = 0;
    int captureFps_ = 30;

    // 核心组件
    ICapture* capture_ = nullptr;
    IEncoder* encoder_ = nullptr;
    std::unique_ptr<HttpPreviewServer> preview_;
    FlvMuxer flvMuxer_;
    RtmpPublisher rtmp_;

    // 控制
    std::atomic<bool> running_{ false };
    std::thread streamThread_;
    mutable std::mutex previewMutex_;
    std::atomic<bool> previewFrameRequested_{false};
    std::vector<uint8_t> latestJpeg_;
};