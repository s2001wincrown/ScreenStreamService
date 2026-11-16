// src/HttpPreviewServer.cpp
#include "HttpPreviewServer.h"
#include <ws2tcpip.h>
#include <Logger.h>
#include <string>
#include <chrono>
#include <algorithm>
#include <charconv>
#include <climits>
#pragma comment(lib, "ws2_32.lib")

namespace {
bool ParseScreenNumber(const std::string& value, int& screenNumber) {
    if (value.empty()) return false;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), screenNumber);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() &&
        screenNumber > 0;
}
}

HttpPreviewServer::HttpPreviewServer(int port, int width, int height,
    int quality, int maxFps, std::vector<int> screenNumbers, FrameGrabber grabber)
    : port_(port), width_(width), height_(height),
      frameIntervalMs_(maxFps > 0 ? (std::max)(1, 1000 / maxFps) : 66),
      screenNumbers_(std::move(screenNumbers)), grabber_(std::move(grabber)) {}

HttpPreviewServer::~HttpPreviewServer() { Stop(); }

void HttpPreviewServer::Start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&HttpPreviewServer::Serve, this);
}

void HttpPreviewServer::Stop() {
    if (!running_.exchange(false)) return;
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (SOCKET client : clients_) shutdown(client, SD_BOTH);
    }
    if (thread_.joinable()) thread_.join();
    for (auto& worker : clientThreads_) {
        if (worker.thread.joinable()) worker.thread.join();
    }
    clientThreads_.clear();
}

bool HttpPreviewServer::SendAll(SOCKET socket, const char* data, size_t size) {
    while (size != 0) {
        const int chunk = size > static_cast<size_t>(INT_MAX)
            ? INT_MAX : static_cast<int>(size);
        const int sent = send(socket, data, chunk, 0);
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

void HttpPreviewServer::ServeClient(SOCKET client) {
    clientCount_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.push_back(client);
    }

    char request[4096]{};
    const int received = recv(client, request, sizeof(request) - 1, 0);
    if (received > 0) {
        CLogger::Message(0, L"HTTP preview client connected; request bytes=%d", received);
        std::string requestLine(request, static_cast<size_t>(received));
        const size_t lineEnd = requestLine.find("\r\n");
        if (lineEnd != std::string::npos) requestLine.resize(lineEnd);
        const size_t firstSpace = requestLine.find(' ');
        const size_t secondSpace = firstSpace == std::string::npos
            ? std::string::npos : requestLine.find(' ', firstSpace + 1);
        std::string path = firstSpace != std::string::npos && secondSpace != std::string::npos
            ? requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1)
            : std::string();
        std::string queryString;
        const size_t query = path.find('?');
        if (query != std::string::npos) {
            queryString = path.substr(query + 1);
            path.resize(query);
        }

        int screenNumber = screenNumbers_.empty() ? 1 : screenNumbers_.front();
        bool validScreen = true;
        const size_t screenPathEnd = path.find('/', 1);
        const std::string screenPath = path.size() > 1
            ? path.substr(1, screenPathEnd == std::string::npos
                ? std::string::npos : screenPathEnd - 1)
            : std::string();
        if (!screenPath.empty()) {
            int pathScreenNumber = 0;
            if (ParseScreenNumber(screenPath, pathScreenNumber)) {
                screenNumber = pathScreenNumber;
                path = screenPathEnd == std::string::npos ? "" : path.substr(screenPathEnd);
            }
        }

        size_t screenParameter = queryString.find("screen=");
        if (screenParameter != std::string::npos) {
            screenParameter += 7;
            const size_t parameterEnd = queryString.find('&', screenParameter);
            const std::string value = queryString.substr(screenParameter,
                parameterEnd == std::string::npos ? std::string::npos : parameterEnd - screenParameter);
            validScreen = ParseScreenNumber(value, screenNumber);
        }
        if (screenNumbers_.empty()) {
            validScreen = validScreen && screenNumber == 1;
        } else {
            validScreen = validScreen && std::find(screenNumbers_.begin(),
                screenNumbers_.end(), screenNumber) != screenNumbers_.end();
        }

        const bool pageRequest = path == "/" || path == "/index.html" || path.empty();
        if (!validScreen) {
            static constexpr char notFound[] =
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(client, notFound, sizeof(notFound) - 1);
        } else if (pageRequest) {
            const std::string page = R"html(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ScreenStream Preview</title><style>
html,body{margin:0;width:100%;height:100%;background:#111;color:#ddd;font:14px sans-serif}
body{display:grid;place-items:center}img{max-width:100vw;max-height:100vh;object-fit:contain}
</style></head><body><img id="preview" alt="Waiting for screen capture...">
<script>const screenNumber=)html" + std::to_string(screenNumber) + R"html(;
const image=document.getElementById('preview');function refresh(){
image.src='/snapshot.jpg?screen='+screenNumber+'&t='+Date.now()}
image.onload=()=>setTimeout(refresh,100);image.onerror=()=>setTimeout(refresh,1000);refresh();
</script></body></html>)html";
            const std::string response =
                "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n" +
                std::string("Content-Length: ") + std::to_string(page.size()) +
                "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + page;
            if (!SendAll(client, response.data(), response.size()))
                CLogger::LogError(L"HTTP preview page send failed",
                    static_cast<DWORD>(WSAGetLastError()));
        } else if (path == "/snapshot.jpg") {
            const auto jpeg = grabber_(screenNumber);
            if (jpeg.empty()) {
                static constexpr char unavailable[] =
                    "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 1\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n";
                SendAll(client, unavailable, sizeof(unavailable) - 1);
            } else {
                const std::string header =
                    "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n" +
                    std::string("Content-Length: ") + std::to_string(jpeg.size()) +
                    "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
                if (!SendAll(client, header.data(), header.size()) ||
                    !SendAll(client, reinterpret_cast<const char*>(jpeg.data()), jpeg.size()))
                    CLogger::LogError(L"HTTP preview snapshot send failed",
                        static_cast<DWORD>(WSAGetLastError()));
            }
        } else if (path == "/stream") {
            const std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                "Pragma: no-cache\r\n"
                "Connection: close\r\n\r\n";
            if (SendAll(client, response.data(), response.size())) {
                auto nextFrame = std::chrono::steady_clock::now();
                bool firstFrameSent = false;
                while (running_) {
                    nextFrame += std::chrono::milliseconds(frameIntervalMs_);
                    auto jpeg = grabber_(screenNumber);
                    if (!jpeg.empty()) {
                        const std::string header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                            std::to_string(jpeg.size()) + "\r\n\r\n";
                        static constexpr char partTerminator[] = "\r\n";
                        if (!SendAll(client, header.data(), header.size()) ||
                            !SendAll(client, reinterpret_cast<const char*>(jpeg.data()), jpeg.size()) ||
                            !SendAll(client, partTerminator, sizeof(partTerminator) - 1)) {
                            CLogger::LogError(L"HTTP preview frame send failed",
                                static_cast<DWORD>(WSAGetLastError()));
                            break;
                        }
                        if (!firstFrameSent) {
                            CLogger::Message(0, L"Sent first HTTP preview frame: %llu bytes",
                                static_cast<unsigned long long>(jpeg.size()));
                            firstFrameSent = true;
                        }
                    }
                    std::this_thread::sleep_until(nextFrame);
                }
            } else {
                CLogger::LogError(L"HTTP preview stream header send failed",
                    static_cast<DWORD>(WSAGetLastError()));
            }
        } else {
            static constexpr char notFound[] =
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(client, notFound, sizeof(notFound) - 1);
        }
    } else {
        CLogger::LogError(L"HTTP preview request receive failed",
            static_cast<DWORD>(WSAGetLastError()));
    }

    shutdown(client, SD_BOTH);
    closesocket(client);
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), client), clients_.end());
    }
    clientCount_.fetch_sub(1);
}

void HttpPreviewServer::Serve() {
    WSADATA wsa{};
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (startupResult != 0) {
        CLogger::LogError(L"Preview server WSAStartup failed", static_cast<DWORD>(startupResult));
        return;
    }

    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCKET) {
        CLogger::LogError(L"Preview server socket creation failed", static_cast<DWORD>(WSAGetLastError()));
        WSACleanup();
        return;
    }
    int reuse = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<u_short>(port_));
    if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        CLogger::LogError(L"Preview server bind/listen failed", static_cast<DWORD>(WSAGetLastError()));
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(listenSocket_, FIONBIO, &nonBlocking) == SOCKET_ERROR)
        CLogger::LogError(L"Preview server ioctlsocket failed", static_cast<DWORD>(WSAGetLastError()));
    CLogger::Message(0, L"Preview server listening on port %d", port_);

    while (running_) {
        auto worker = clientThreads_.begin();
        while (worker != clientThreads_.end()) {
            if (worker->completed->load()) {
                if (worker->thread.joinable()) worker->thread.join();
                worker = clientThreads_.erase(worker);
            } else {
                ++worker;
            }
        }

        sockaddr_in clientAddress{};
        int clientLength = sizeof(clientAddress);
        SOCKET client = accept(listenSocket_, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (client == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        u_long blocking = 0;
        if (ioctlsocket(client, FIONBIO, &blocking) == SOCKET_ERROR) {
            CLogger::LogError(L"HTTP preview client socket setup failed",
                static_cast<DWORD>(WSAGetLastError()));
            closesocket(client);
            continue;
        }
        const DWORD receiveTimeoutMs = 5000;
        if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&receiveTimeoutMs), sizeof(receiveTimeoutMs)) == SOCKET_ERROR) {
            CLogger::LogError(L"HTTP preview client receive timeout setup failed",
                static_cast<DWORD>(WSAGetLastError()));
            closesocket(client);
            continue;
        }

        auto completed = std::make_shared<std::atomic<bool>>(false);
        ClientThread clientThread;
        clientThread.completed = completed;
        clientThread.thread = std::thread([this, client, completed] {
            ServeClient(client);
            completed->store(true);
        });
        clientThreads_.push_back(std::move(clientThread));
    }

    if (listenSocket_ != INVALID_SOCKET) closesocket(listenSocket_);
    listenSocket_ = INVALID_SOCKET;
    WSACleanup();
}