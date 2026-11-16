// src/ScreenStreamService.cpp
#include "ScreenStreamService.h"
#include "Config.h"
#include "DxCapture.h"
#include "EncoderFactory.h"
#include "JpegEncoder.h"
#include <Logger.h>
#include <chrono>
#include <algorithm>
#include <charconv>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace {
bool ParseMonitorList(const std::string& value, std::vector<int>& monitors) {
    size_t start = 0;
    while (start < value.size()) {
        const size_t end = value.find(',', start);
        const size_t tokenEnd = end == std::string::npos ? value.size() : end;
        const size_t first = value.find_first_not_of(" \t", start);
        if (first == std::string::npos || first >= tokenEnd) return false;
        const size_t last = value.find_last_not_of(" \t", tokenEnd - 1);
        int monitor = -1;
        const auto result = std::from_chars(value.data() + first, value.data() + last + 1, monitor);
        if (result.ec != std::errc{} || result.ptr != value.data() + last + 1 ||
            monitor < 0 || monitor == (std::numeric_limits<int>::max)() ||
            std::find(monitors.begin(), monitors.end(), monitor) != monitors.end())
            return false;
        monitors.push_back(monitor);
        if (end == std::string::npos) break;
        if (end + 1 == value.size()) return false;
        start = end + 1;
    }
    return !monitors.empty();
}
}

struct ScreenStreamService::ScreenPipeline {
    int monitorIndex = 0;
    int screenNumber = 1;
    std::string rtmpUrl;
    std::unique_ptr<ICapture> capture;
    std::unique_ptr<IEncoder> encoder;
    FlvMuxer flvMuxer;
    RtmpPublisher publisher;
    std::mutex previewMutex;
    std::atomic<bool> previewFrameRequested{ false };
    std::vector<uint8_t> latestJpeg;
};

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

    rtmpEnabled_ = cfg.GetBool("rtmp", "enabled", true);
    if (rtmpEnabled_) {
        rtmpUrl_ = cfg.Get("rtmp", "url", "rtmp://127.0.0.1/live/stream");
        bitrate_ = cfg.GetInt("rtmp", "bitrate", 4000);
        fps_ = cfg.GetInt("rtmp", "fps", 30);
        gop_ = cfg.GetInt("rtmp", "gop", 60);
        width_ = cfg.GetInt("rtmp", "width", 1920);
        height_ = cfg.GetInt("rtmp", "height", 1080);
    }

    previewEnabled_ = cfg.GetBool("preview", "enabled", true);
    if (previewEnabled_) {
        previewPort_ = cfg.GetInt("preview", "port", 8888);
        previewWidth_ = cfg.GetInt("preview", "width", 640);
        previewHeight_ = cfg.GetInt("preview", "height", 360);
        previewQuality_ = cfg.GetInt("preview", "quality", 70);
        previewMaxFps_ = cfg.GetInt("preview", "max_fps", 15);
    }

    const std::string monitors = cfg.Get("capture", "monitors");
    if (monitors.empty()) {
        const int monitor = cfg.GetInt("capture", "monitor", 0);
        if (monitor < 0 || monitor == (std::numeric_limits<int>::max)()) {
            CLogger::LogMessage(L"Invalid capture monitor index in configuration");
            if (errorCode) *errorCode = ERROR_INVALID_PARAMETER;
            return false;
        }
        captureMonitors_.push_back(monitor);
    } else if (!ParseMonitorList(monitors, captureMonitors_)) {
        CLogger::LogMessage(L"Invalid capture monitors list; use unique non-negative indices separated by commas");
        if (errorCode) *errorCode = ERROR_INVALID_PARAMETER;
        return false;
    }
    captureFps_ = cfg.GetInt("capture", "fps", 30);

    for (const int monitorIndex : captureMonitors_) {
        auto screen = std::make_unique<ScreenPipeline>();
        screen->monitorIndex = monitorIndex;
        screen->screenNumber = monitorIndex + 1;
        screen->capture.reset(CreateCapture(monitorIndex, captureFps_));
        if (!screen->capture) {
            CLogger::LogMessage(L"Failed to create capture instance");
            if (errorCode) *errorCode = ERROR_GEN_FAILURE;
            return false;
        }

        if (rtmpEnabled_) {
            screen->rtmpUrl = rtmpUrl_;
            if (captureMonitors_.size() > 1) {
                if (!screen->rtmpUrl.empty() && screen->rtmpUrl.back() != '/')
                    screen->rtmpUrl += '/';
                screen->rtmpUrl += std::to_string(screen->screenNumber);
            }
            screen->encoder = EncoderFactory::Create(
                width_, height_, fps_, bitrate_ * 1000, gop_);
            if (!screen->encoder) {
                CLogger::LogMessage(L"Failed to open an encoder for a configured screen");
                if (errorCode) *errorCode = ERROR_NOT_SUPPORTED;
                return false;
            }
            screen->flvMuxer.Open();
        }
        screens_.push_back(std::move(screen));
    }

    if (previewEnabled_) {
        std::vector<int> screenNumbers;
        screenNumbers.reserve(screens_.size());
        for (const auto& screen : screens_)
            screenNumbers.push_back(screen->screenNumber);
        preview_ = std::make_unique<HttpPreviewServer>(
            previewPort_, previewWidth_, previewHeight_,
            previewQuality_, previewMaxFps_, std::move(screenNumbers),
            [this](int screenNumber) { return GrabPreviewFrame(screenNumber); });
    }

    CLogger::Message(0, L"Initialized: RTMP=%s, preview=%s, screens=%d, capture fps=%d",
        rtmpEnabled_ ? L"ON" : L"OFF", previewEnabled_ ? L"ON" : L"OFF",
        static_cast<int>(screens_.size()), captureFps_);
    return true;
}

bool ScreenStreamService::Start(DWORD* errorCode) {
    if (running_) {
        if (errorCode) *errorCode = ERROR_ALREADY_INITIALIZED;
        return false;
    }
    if (screens_.empty()) {
        CLogger::LogMessage(L"Cannot start application: capture is not initialized");
        if (errorCode) *errorCode = ERROR_INVALID_STATE;
        return false;
    }

    for (const auto& screen : screens_) {
        if (!screen->capture->Start()) {
            CLogger::Message(0, L"Desktop capture startup failed for screen %d; see preceding diagnostics",
                screen->screenNumber);
            for (const auto& startedScreen : screens_)
                startedScreen->capture->Stop();
            if (errorCode) *errorCode = ERROR_GEN_FAILURE;
            return false;
        }
    }

    if (previewEnabled_) preview_->Start();

    running_ = true;
    streamThread_ = std::thread(&ScreenStreamService::StreamLoop, this);
    CLogger::LogMessage(L"Desktop application runtime started");
    return true;
}

void ScreenStreamService::Stop() {
    const bool wasRunning = running_.exchange(false);
    if (streamThread_.joinable()) streamThread_.join();

    if (preview_) preview_->Stop();
    for (const auto& screen : screens_) {
        screen->publisher.Close();
        screen->capture->Stop();
        if (screen->encoder) screen->encoder->Close();
    }

    if (wasRunning)
        CLogger::LogMessage(L"Desktop application runtime stopped");
}

void ScreenStreamService::StreamLoop() {
    const int frameRate = rtmpEnabled_ ? fps_ : captureFps_;
    const auto frameInterval = std::chrono::microseconds(
        1000000 / (frameRate > 0 ? frameRate : 30));
    auto lastFrame = std::chrono::steady_clock::now();
    const int previewFps = previewMaxFps_ > 0 ? previewMaxFps_ : 15;
    const auto previewInterval = std::chrono::milliseconds((std::max)(1, 1000 / previewFps));
    std::vector<bool> rtmpConnected(screens_.size(), false);
    std::vector<bool> previewFrameLogged(screens_.size(), false);
    std::vector<std::chrono::steady_clock::time_point> nextRtmpAttempt(screens_.size(), lastFrame);
    std::vector<std::chrono::steady_clock::time_point> nextPreviewFrame(screens_.size(), lastFrame);
    std::vector<std::chrono::steady_clock::time_point> nextPreviewFailureLog(screens_.size(), lastFrame);

    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        if (rtmpEnabled_) {
            for (size_t i = 0; i < screens_.size(); ++i) {
                auto& screen = *screens_[i];
                if (!rtmpConnected[i] && now >= nextRtmpAttempt[i]) {
                    rtmpConnected[i] = screen.publisher.Connect(screen.rtmpUrl, "", "");
                    if (rtmpConnected[i]) {
                        screen.flvMuxer.Open();
                        CLogger::Message(0, L"RTMP connected for screen %d",
                            screen.screenNumber);
                    } else {
                        CLogger::Message(0, L"RTMP connection failed for screen %d; retrying in 5 seconds",
                            screen.screenNumber);
                        nextRtmpAttempt[i] = now + std::chrono::seconds(5);
                    }
                }
            }
        }

        const auto elapsed = now - lastFrame;
        if (elapsed >= frameInterval) {
            lastFrame = now;
            for (size_t i = 0; i < screens_.size(); ++i) {
                auto& screen = *screens_[i];
                auto texture = screen.capture->Capture();
                if (!texture) continue;

                const auto previewNow = std::chrono::steady_clock::now();
                if (previewEnabled_ && preview_ && screen.previewFrameRequested.load() &&
                    previewNow >= nextPreviewFrame[i] &&
                    screen.previewFrameRequested.exchange(false)) {
                    nextPreviewFrame[i] = previewNow + previewInterval;
                    auto jpeg = JpegEncoder::Encode(texture,
                        screen.capture->Width(), screen.capture->Height(),
                        previewWidth_, previewHeight_, previewQuality_);
                    if (!jpeg.empty()) {
                        const size_t jpegSize = jpeg.size();
                        {
                            std::lock_guard<std::mutex> lock(screen.previewMutex);
                            screen.latestJpeg = std::move(jpeg);
                        }
                        if (!previewFrameLogged[i]) {
                            CLogger::Message(0, L"HTTP preview JPEG cache ready for screen %d: %llu bytes, %dx%d",
                                screen.screenNumber, static_cast<unsigned long long>(jpegSize),
                                previewWidth_, previewHeight_);
                            previewFrameLogged[i] = true;
                        }
                    } else {
                        if (preview_->ClientCount() > 0)
                            screen.previewFrameRequested.store(true);
                        if (previewNow >= nextPreviewFailureLog[i]) {
                            CLogger::Message(0, L"HTTP preview JPEG encoding failed for screen %d",
                                screen.screenNumber);
                            nextPreviewFailureLog[i] = previewNow + std::chrono::seconds(5);
                        }
                    }
                }

                if (rtmpEnabled_ && rtmpConnected[i] && screen.encoder) {
                    auto frame = screen.encoder->Encode(texture);
                    if (!frame.annexb.empty()) {
                        for (const auto& packet : screen.flvMuxer.WriteVideo(frame.annexb)) {
                            if (!screen.publisher.Send(packet.data)) {
                                CLogger::Message(0, L"RTMP connection lost for screen %d; retrying in 5 seconds",
                                    screen.screenNumber);
                                screen.publisher.Close();
                                rtmpConnected[i] = false;
                                nextRtmpAttempt[i] = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(5);
                                break;
                            }
                        }
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(frameInterval - elapsed);
        }
    }
}

std::vector<uint8_t> ScreenStreamService::GrabPreviewFrame(int screenNumber) {
    if (!preview_ || preview_->ClientCount() <= 0) return {};
    for (const auto& screen : screens_) {
        if (screen->screenNumber == screenNumber) {
            screen->previewFrameRequested.store(true);
            std::lock_guard<std::mutex> lock(screen->previewMutex);
            return screen->latestJpeg;
        }
    }
    return {};
}
