#pragma once
// Minimal Winsock protocol client for Memory Expansion Fabric multiprocess tests.
#include "memory_expansion_fabric/protocol.hpp"

#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  undef ERROR
#endif

#include <cstdio>
#include <string>
#include <vector>

namespace mef = memory_expansion_fabric;

class MefClient {
public:
    MefClient() = default;
    ~MefClient() {
#ifdef _WIN32
        if (s_ != INVALID_SOCKET) { ::closesocket(s_); s_ = INVALID_SOCKET; }
#endif
    }
    MefClient(const MefClient&) = delete;
    MefClient& operator=(const MefClient&) = delete;

    bool connectAny(const std::string& host, std::uint16_t port, int maxTries = 600) {
#ifdef _WIN32
        static bool started = false;
        if (!started) { WSADATA wsa; if (::WSAStartup(MAKEWORD(2,2), &wsa) != 0) return false; started = true; }
        for (int t = 0; t < maxTries; ++t) {
            SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET) return false;
            sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
            ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
            if (::connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) { s_ = s; return true; }
            ::closesocket(s);
            ::Sleep(10);
        }
#else
        (void)host; (void)port; (void)maxTries;
#endif
        return false;
    }

    bool sendFrame(mef::FrameType type, const std::vector<std::uint8_t>& payload) {
        auto enc = mef::encodeFrame(type, payload);
        if (!enc.ok()) return false;
#ifdef _WIN32
        const auto& b = enc.value();
        std::size_t off = 0;
        while (off < b.size()) {
            int n = ::send(s_, (const char*)(b.data()+off), (int)(b.size()-off), 0);
            if (n <= 0) return false;
            off += (std::size_t)n;
        }
        return true;
#else
        (void)payload; return false;
#endif
    }

    bool recvFrame(mef::Frame& out) {
#ifdef _WIN32
        for (;;) {
            mef::Frame frame; std::size_t consumed = 0; std::string err;
            mef::FrameDecode st = mef::decodeFrame(buf_.data(), buf_.size(), frame, consumed, err);
            if (st == mef::FrameDecode::OK) { buf_.erase(buf_.begin(), buf_.begin()+consumed); out = std::move(frame); return true; }
            if (st == mef::FrameDecode::ERROR) return false;
            char tmp[4096];
            int n = ::recv(s_, tmp, sizeof(tmp), 0);
            if (n <= 0) return false;
            buf_.insert(buf_.end(), tmp, tmp+n);
        }
#else
        (void)out; return false;
#endif
    }

    bool request(mef::FrameType reqType, const std::vector<std::uint8_t>& payload,
                 mef::FrameType expectType, std::vector<std::uint8_t>& out) {
        if (!sendFrame(reqType, payload)) return false;
        for (;;) {
            mef::Frame f;
            if (!recvFrame(f)) return false;
            if (f.type == expectType) { out = std::move(f.payload); return true; }
            if (f.type == mef::FrameType::HELLO_ACK) continue;
        }
    }
private:
#ifdef _WIN32
    SOCKET s_ = INVALID_SOCKET;
#endif
    std::vector<std::uint8_t> buf_;
};
