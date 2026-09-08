// Memory Expansion Fabric - reference worker (Windows).
#include "memory_expansion_fabric/protocol.hpp"

#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  undef ERROR
#endif

#include <cstdio>
#include <string>
#include <vector>

namespace mef = memory_expansion_fabric;

int runWorker(int argc, char** argv) {
    std::uint64_t port = 0, wid = 0, boot = 0, epoch = 1;
    std::uint64_t region = 1, provider = 1, online = 0, latency = 100, bw = 1000000000, egen = 1, eid = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i+1 < argc) port = std::stoull(argv[++i]);
        else if (a == "--worker" && i+1 < argc) wid = std::stoull(argv[++i]);
        else if (a == "--boot" && i+1 < argc) boot = std::stoull(argv[++i]);
        else if (a == "--epoch" && i+1 < argc) epoch = std::stoull(argv[++i]);
        else if (a == "--region" && i+1 < argc) region = std::stoull(argv[++i]);
        else if (a == "--provider" && i+1 < argc) provider = std::stoull(argv[++i]);
        else if (a == "--online" && i+1 < argc) online = std::stoull(argv[++i]);
        else if (a == "--latency" && i+1 < argc) latency = std::stoull(argv[++i]);
        else if (a == "--bandwidth" && i+1 < argc) bw = std::stoull(argv[++i]);
        else if (a == "--egen" && i+1 < argc) egen = std::stoull(argv[++i]);
        else if (a == "--eid" && i+1 < argc) eid = std::stoull(argv[++i]);
    }
    if (port == 0) { std::printf("worker: --port required\n"); return 1; }

#ifdef _WIN32
    WSADATA wsa;
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::printf("worker: WSAStartup failed\n"); return 1; }
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { std::printf("worker: socket failed\n"); return 1; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    bool connected = false;
    for (int tries = 0; tries < 200 && !connected; ++tries) {
        if (::connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) connected = true;
        else ::Sleep(25);
    }
    if (!connected) { std::printf("worker: connect failed\n"); return 1; }

    std::vector<std::uint8_t> hello;
    auto put64 = [&](std::uint64_t v){ for (int i = 0; i < 8; ++i) hello.push_back((uint8_t)((v >> (8*i)) & 0xFFu)); };
    put64(wid); put64(boot); put64(epoch);
    put64((std::uint64_t)::GetCurrentProcessId());
    auto encHello = mef::encodeFrame(mef::FrameType::HELLO, hello);
    if (!encHello.ok()) return 1;
    ::send(s, (const char*)encHello.value().data(), (int)encHello.value().size(), 0);

    mef::RegionEvidence ev;
    ev.header.id = mef::EvidenceId(eid);
    ev.header.generation = mef::EvidenceGeneration(egen);
    ev.header.sourceProvider = mef::ProviderId(provider);
    ev.header.sourceProviderGeneration = mef::ProviderGeneration(1);
    ev.header.region = mef::RegionId(region);
    ev.header.regionGeneration = mef::RegionGeneration(1);
    ev.header.worker = mef::WorkerId(wid);
    ev.header.boot = mef::WorkerBootId(boot);
    ev.header.epoch = mef::CoordinatorEpoch(epoch);
    ev.header.provenance = "synthetic-worker";
    ev.health = mef::HealthState::HEALTHY;
    ev.reachable = true;
    ev.onlineCapacityBytes = online;
    ev.latencyNs = latency;
    ev.bandwidthBytesPerSec = bw;
    ev.locality = mef::Locality::LOCAL;
    ev.status = mef::EvidenceStatus::CURRENT;
    auto evEnc = mef::encodeRegionEvidence(ev);
    if (!evEnc.ok()) return 1;
    auto encEv = mef::encodeFrame(mef::FrameType::PUBLISH_EVIDENCE, evEnc.value());
    if (!encEv.ok()) return 1;
    ::send(s, (const char*)encEv.value().data(), (int)encEv.value().size(), 0);

    // Stay alive until terminated as a real OS process.
    for (;;) {
        char tmp[512];
        int n = ::recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
    }
    ::closesocket(s);
#else
    (void)port; (void)wid; (void)boot; (void)epoch; (void)region; (void)provider;
    (void)online; (void)latency; (void)bw; (void)egen; (void)eid;
#endif
    return 0;
}
