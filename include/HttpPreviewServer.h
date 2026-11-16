// include/HttpPreviewServer.h
#pragma once
#include <atomic>
#include <thread>
#include <functional>
#include <vector>
#include <cstdint>
#include <mutex>
#include <memory>
#include <winsock2.h>

class HttpPreviewServer {
public:
    using FrameGrabber = std::function<std::vector<uint8_t>(int screenNumber)>;

    HttpPreviewServer(int port, int width, int height,
        int quality, int maxFps, std::vector<int> screenNumbers, FrameGrabber grabber);
    ~HttpPreviewServer();

    void Start();
    void Stop();
    int ClientCount() const { return clientCount_.load(); }

private:
    void Serve();
    void ServeClient(SOCKET client);
    static bool SendAll(SOCKET socket, const char* data, size_t size);

    int port_, width_, height_, frameIntervalMs_;
    std::vector<int> screenNumbers_;
    FrameGrabber grabber_;
    std::atomic<bool> running_{false};
    std::atomic<int> clientCount_{0};
    std::thread thread_;
    SOCKET listenSocket_ = INVALID_SOCKET;
    struct ClientThread {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> completed;
    };

    std::mutex clientsMutex_;
    std::vector<SOCKET> clients_;
    std::vector<ClientThread> clientThreads_;
};