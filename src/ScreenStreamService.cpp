// src/ScreenStreamService.cpp
#include "ScreenStreamService.h"
#include "Config.h"
#include "DxCapture.h"
#include "EncoderFactory.h"
#include "JpegEncoder.h"
#include <Logger.h>
#include <chrono>

ScreenStreamService::ScreenStreamService() {}

ScreenStreamService::~ScreenStreamService() {
    Stop();
}

bool ScreenStreamService::Initialize(const std::filesystem::path& configPath,
                                      DWORD* errorCode) {
    auto& cfg = Config::Instance();
    if (!cfg.Load(configPath)) {
        CLogger::Message(0, L"Failed to load config file: %s", configPath.c_str());
        if (errorCode) *errorCode = ERROR_FILE_NOT_FOUND;
        return false;
    }

    // 读取 RTMP 配置
    rtmpEnabled_ = cfg.GetBool("rtmp", "enabled", true);
    if (rtmpEnabled_) {
        rtmpUrl_ = cfg.Get("rtmp", "url", "rtmp://127.0.0.1/live/stream");
        bitrate_ = cfg.GetInt("rtmp", "bitrate", 4000);
        fps_ = cfg.GetInt("rtmp", "fps", 30);
        gop_ = cfg.GetInt("rtmp", "gop", 60);
        width_ = cfg.GetInt("rtmp", "width", 1920);
        height_ = cfg.GetInt("rtmp", "height", 1080);
    }

    // 读取预览配置
    previewEnabled_ = cfg.GetBool("preview", "enabled", true);
    if (previewEnabled_) {
        previewPort_ = cfg.GetInt("preview", "port", 8888);
        previewWidth_ = cfg.GetInt("preview", "width", 640);
        previewHeight_ = cfg.GetInt("preview", "height", 360);
        previewQuality_ = cfg.GetInt("preview", "quality", 70);
        previewMaxFps_ = cfg.GetInt("preview", "max_fps", 15);
    }

    // 读取采集配置
    captureMonitor_ = cfg.GetInt("capture", "monitor", 0);
    captureFps_ = cfg.GetInt("capture", "fps", 30);

    // 创建采集器
    CLogger::Message(0, L"Initializing desktop capture: monitor=%d, fps=%d",
        captureMonitor_, captureFps_);
    capture_ = CreateCapture(captureMonitor_, captureFps_);
    if (!capture_) {
        CLogger::LogMessage(L"Failed to create capture instance");
        if (errorCode) *errorCode = ERROR_GEN_FAILURE;
        return false;
    }

    // 创建编码器（仅在 RTMP 启用时）
    if (rtmpEnabled_) {
        auto encoder = EncoderFactory::Create(width_, height_, fps_, bitrate_ * 1000, gop_);
        encoder_ = encoder.release();
        if (!encoder_) {
            CLogger::LogMessage(L"Failed to open any supported video encoder");
            if (errorCode) *errorCode = ERROR_NOT_SUPPORTED;
            return false;
        }
        flvMuxer_.Open();
    }

    // 创建预览服务器（仅在预览启用时）
    if (previewEnabled_) {
        preview_ = std::make_unique<HttpPreviewServer>(
            previewPort_, previewWidth_, previewHeight_,
            previewQuality_, previewMaxFps_,
            [this] { return GrabPreviewFrame(); });
    }

    CLogger::Message(0, L"Initialized: RTMP=%s, preview=%s, monitor=%d, capture fps=%d",
        rtmpEnabled_ ? L"ON" : L"OFF", previewEnabled_ ? L"ON" : L"OFF",
        captureMonitor_, captureFps_);
    return true;
}

bool ScreenStreamService::Start(DWORD* errorCode) {
    if (running_) {
        if (errorCode) *errorCode = ERROR_ALREADY_INITIALIZED;
        return false;
    }
    if (!capture_) {
        CLogger::LogMessage(L"Cannot start application: capture is not initialized");
        if (errorCode) *errorCode = ERROR_INVALID_STATE;
        return false;
    }

    if (!capture_->Start()) {
        CLogger::LogMessage(L"Desktop capture startup failed; see preceding DXGI/D3D11 diagnostics");
        if (errorCode) *errorCode = ERROR_GEN_FAILURE;
        return false;
    }

    if (previewEnabled_) {
        preview_->Start();
    }

    running_ = true;
    streamThread_ = std::thread(&ScreenStreamService::StreamLoop, this);

    CLogger::LogMessage(L"Desktop application runtime started");
    return true;
}

void ScreenStreamService::Stop() {
    const bool wasRunning = running_.exchange(false);

    if (streamThread_.joinable()) streamThread_.join();

    if (preview_) preview_->Stop();
    if (rtmpEnabled_) rtmp_.Close();
    if (capture_) capture_->Stop();

    if (encoder_) {
        encoder_->Close();
        delete encoder_;
        encoder_ = nullptr;
    }
    if (capture_) {
        delete capture_;
        capture_ = nullptr;
    }

    if (wasRunning)
        CLogger::LogMessage(L"Desktop application runtime stopped");
}

void ScreenStreamService::StreamLoop() {
    auto frameInterval = std::chrono::microseconds(1000000 / fps_);
    auto lastFrame = std::chrono::steady_clock::now();
    auto nextRtmpAttempt = lastFrame;
    const int previewFps = previewMaxFps_ > 0 ? previewMaxFps_ : 15;
    const auto previewInterval = std::chrono::milliseconds((std::max)(1, 1000 / previewFps));
    auto nextPreviewFrame = lastFrame;
    auto nextPreviewFailureLog = lastFrame;
    bool rtmpConnected = false;
    bool previewFrameLogged = false;

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        if (rtmpEnabled_ && !rtmpConnected && now >= nextRtmpAttempt) {
            rtmpConnected = rtmp_.Connect(rtmpUrl_, "", "");
            if (!rtmpConnected) {
                CLogger::LogMessage(L"RTMP connection failed; retrying in 5 seconds");
                nextRtmpAttempt = now + std::chrono::seconds(5);
            }
        }

        auto elapsed = now - lastFrame;

        if (elapsed >= frameInterval) {
            lastFrame = now;

            auto texture = capture_->Capture();
            if (!texture) continue;

            const auto previewNow = std::chrono::steady_clock::now();
            if (previewEnabled_ && preview_ && previewFrameRequested_.load() &&
                previewNow >= nextPreviewFrame && previewFrameRequested_.exchange(false)) {
                nextPreviewFrame = previewNow + previewInterval;
                auto jpeg = JpegEncoder::Encode(texture, capture_->Width(), capture_->Height(),
                    previewWidth_, previewHeight_, previewQuality_);
                if (!jpeg.empty()) {
                    const size_t jpegSize = jpeg.size();
                    {
                        std::lock_guard<std::mutex> lock(previewMutex_);
                        latestJpeg_ = std::move(jpeg);
                    }
                    if (!previewFrameLogged) {
                        CLogger::Message(0, L"HTTP preview JPEG cache ready on demand: %llu bytes, %dx%d",
                            static_cast<unsigned long long>(jpegSize), previewWidth_, previewHeight_);
                        previewFrameLogged = true;
                    }
                } else {
                    if (preview_->ClientCount() > 0)
                        previewFrameRequested_.store(true);
                    if (previewNow >= nextPreviewFailureLog) {
                        CLogger::LogMessage(L"HTTP preview JPEG encoding returned an empty frame");
                        nextPreviewFailureLog = previewNow + std::chrono::seconds(5);
                    }
                }
            }

            // RTMP 主链路
            if (rtmpEnabled_ && rtmpConnected && encoder_) {
                auto frame = encoder_->Encode(texture);
                if (!frame.annexb.empty()) {
                    for (const auto& packet : flvMuxer_.WriteVideo(frame.annexb)) {
                        if (!rtmp_.Send(packet.data)) {
                            CLogger::LogMessage(L"RTMP connection lost; retrying in 5 seconds");
                            rtmp_.Close();
                            rtmpConnected = false;
                            nextRtmpAttempt = std::chrono::steady_clock::now() +
                                std::chrono::seconds(5);
                            break;
                        }
                    }
                }
            }
        }
        else {
            std::this_thread::sleep_for(frameInterval - elapsed);
        }
    }
}

std::vector<uint8_t> ScreenStreamService::GrabPreviewFrame() {
    if (!preview_ || preview_->ClientCount() <= 0) return {};
    previewFrameRequested_.store(true);
    std::lock_guard<std::mutex> lock(previewMutex_);
    return latestJpeg_;
}
