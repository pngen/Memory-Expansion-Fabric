// Memory Expansion Fabric - reference coordinator (Windows, framed TCP).
// Stands up a Fabric, accepts worker/client connections, spawns worker child
// processes, and can terminate them (the coordinator owns their process
// handles, which it uses to prove worker death).
#include "memory_expansion_fabric/fabric.hpp"
#include "memory_expansion_fabric/protocol.hpp"

#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  undef ERROR
#endif

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>
#include <fstream>
#include <string>

namespace mef = memory_expansion_fabric;

namespace {
void coordTrace(const std::string& msg) {
    std::ofstream f("mef_coord.trace", std::ios::app);
    f << msg << "\n";
}
} // namespace

namespace {

#ifdef _WIN32
using SOCK = SOCKET;
#else
using SOCK = int;
#endif

struct Coordinator {
    mef::Fabric fabric;
    std::uint64_t port = 0;
    std::string workerExe;
    mef::CoordinatorEpoch epoch;
    std::mutex procMtx;
    std::map<mef::WorkerId, HANDLE> procHandles;
    std::mutex sendMtx;

    explicit Coordinator(mef::CoordinatorEpoch e) : fabric(e), epoch(e) {}
    explicit Coordinator(mef::Fabric&& f) : fabric(std::move(f)), epoch(mef::CoordinatorEpoch(1)) {
        epoch = fabric.epoch();
    }
    std::uint64_t regionTotal(mef::RegionId r) const {
        auto snap = fabric.snapshot();
        for (const auto& rg : snap.regions) if (rg.id == r) return rg.totalCapacityBytes;
        return 0;
    }
};
using CoordPtr = std::shared_ptr<Coordinator>;
constexpr std::uint64_t kSeedTotal = 16ull * 1024ull * 1024ull * 1024ull; // 16 GiB

std::uint64_t memefle64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (static_cast<std::uint64_t>(p[i]) << (8*i));
    return v;
}

bool sendAll(SOCK s, const std::vector<std::uint8_t>& bytes) {
    std::size_t off = 0;
    while (off < bytes.size()) {
#ifdef _WIN32
        int n = ::send(s, reinterpret_cast<const char*>(bytes.data() + off),
                       static_cast<int>(bytes.size() - off), 0);
#else
        int n = ::send(s, reinterpret_cast<const char*>(bytes.data() + off),
                       static_cast<int>(bytes.size() - off), 0);
#endif
        if (n <= 0) return false;
        off += static_cast<std::size_t>(n);
    }
    return true;
}

bool sendFrame(CoordPtr c, SOCK s, mef::FrameType type, const std::vector<std::uint8_t>& payload) {
    auto enc = mef::encodeFrame(type, payload);
    if (!enc.ok()) return false;
    std::lock_guard<std::mutex> lk(c->sendMtx);
    return sendAll(s, enc.value());
}

std::string findWorkerExe(const std::string& selfPath) {
    // Derive mef_worker.exe from the coordinator's own directory.
    auto slash = selfPath.find_last_of("\\/");
    std::string dir = slash == std::string::npos ? std::string(".") : selfPath.substr(0, slash);
    return dir + "\\mef_worker.exe";
}

// Spawn a worker subprocess. Returns true on success.
bool spawnWorker(CoordPtr c, mef::WorkerId w, mef::WorkerBootId boot, HANDLE& outHandle) {
    std::string selfPath = "";
    // get own module path
    char buf[4096];
#ifdef _WIN32
    GetModuleFileNameA(nullptr, buf, sizeof(buf));
    selfPath = buf;
#endif
    std::string exe = c->workerExe.empty() ? findWorkerExe(selfPath) : c->workerExe;
    if (exe.empty()) return false;

    std::uint64_t total = c->regionTotal(mef::RegionId(1));
    if (total == 0) total = kSeedTotal;
    std::string cmd = "\"" + exe + "\" --port " + std::to_string(c->port) +
                      " --worker " + std::to_string(w.value()) +
                      " --boot " + std::to_string(boot.value()) +
                      " --epoch " + std::to_string(c->epoch.value()) +
                      " --region 1 --provider 1 --online " + std::to_string(total);

#ifdef _WIN32
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    si.dwFlags = STARTF_USESTDHANDLES;
    // Keep the child's std handles as inheritable but closed; never show a window.
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    DWORD createFlags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
    char* mutableCmd = const_cast<char*>(cmd.c_str());
    BOOL ok = ::CreateProcessA(nullptr, mutableCmd, nullptr, nullptr, FALSE, createFlags,
                               nullptr, nullptr, &si, &pi);
    if (!ok) { std::printf("coordinator: CreateProcess failed %u\n", ::GetLastError()); coordTrace(("CreateProcess failed " + std::to_string(::GetLastError())).c_str()); return false; }
    ::CloseHandle(pi.hThread);
    outHandle = pi.hProcess;
    std::printf("coordinator: spawned worker pid=%u port=%llu\n", pi.dwProcessId, (unsigned long long)c->port);
    coordTrace("spawned worker pid=" + std::to_string(pi.dwProcessId));
#else
    (void)w; (void)boot; (void)outHandle; (void)cmd;
    return false;
#endif
    return true;
}

void workerMonitor(CoordPtr c, mef::WorkerId w, HANDLE h) {
#ifdef _WIN32
    ::WaitForSingleObject(h, INFINITE);   // blocks until the child exits
    coordTrace("workerMonitor: exit detected worker=" + std::to_string(w.value()));
    mef::Authority auth(c->epoch);
    auto md = c->fabric.markWorkerDead(w, auth);
    coordTrace("workerMonitor: markWorkerDead ok=" + std::to_string(md.ok()));
    std::lock_guard<std::mutex> lk(c->procMtx);
    c->procHandles.erase(w);
    ::CloseHandle(h);
#else
    (void)c; (void)w; (void)h;
#endif
}

void handleConnection(CoordPtr c, SOCK s) {
    bool isWorker = false;
    mef::WorkerId worker;
    mef::WorkerBootId boot;
    std::vector<std::uint8_t> buf;

    bool open = true;
    while (open) {
        std::uint8_t tmp[4096];
#ifdef _WIN32
        int n = ::recv(s, reinterpret_cast<char*>(tmp), sizeof(tmp), 0);
#else
        int n = ::recv(s, reinterpret_cast<char*>(tmp), sizeof(tmp), 0);
#endif
        if (n <= 0) { open = false; break; }
        buf.insert(buf.end(), tmp, tmp + n);
        for (;;) {
            mef::Frame frame;
            std::size_t consumed = 0;
            std::string err;
            mef::FrameDecode st = mef::decodeFrame(buf.data(), buf.size(), frame, consumed, err);
            if (st == mef::FrameDecode::INCOMPLETE) break;
            if (st == mef::FrameDecode::ERROR) {
                std::printf("coordinator: frame error: %s\n", err.c_str());
                open = false; break;
            }
            buf.erase(buf.begin(), buf.begin() + consumed);

            // Dispatch.
            switch (frame.type) {
                case mef::FrameType::HELLO: {
                    // payload: worker(u64) boot(u64) epoch(u64) pid(u64)
                    if (frame.payload.size() < 32) break;
                    auto rd = [&](std::size_t off){ return memefle64(frame.payload.data()+off); };
                    worker = mef::WorkerId(rd(0)); boot = mef::WorkerBootId(rd(8));
                    mef::CoordinatorEpoch ep = mef::CoordinatorEpoch(rd(16));
                    std::uint64_t pid = rd(24);
                    mef::Authority a(c->epoch);
                    auto r = c->fabric.registerWorker(worker, boot, ep, pid, a);
                    isWorker = r.ok();
                    if (isWorker) { c->fabric.noteWorkerHandle(worker, true); }
                    std::printf("coordinator: HELLO worker=%llu boot=%llu ok=%d\n",
                                (unsigned long long)worker.value(), (unsigned long long)boot.value(), isWorker?1:0);
                    coordTrace("HELLO worker=" + std::to_string(worker.value()) + " ok=" + std::to_string(isWorker));
                    std::vector<std::uint8_t> ack;
                    ack.push_back(isWorker ? 1 : 0);
                    sendFrame(c, s, mef::FrameType::HELLO_ACK, ack);
                    break;
                }
                case mef::FrameType::PUBLISH_EVIDENCE: {
                    if (!isWorker) { break; }
                    auto ev = mef::decodeRegionEvidence(frame.payload);
                    if (ev.ok()) {
                        mef::Authority a(c->epoch, worker, boot);
                        auto pub = c->fabric.publishRegionEvidence(ev.value(), a);
                        std::printf("coordinator: publish evidence region=%llu ok=%d\n",
                                    (unsigned long long)ev.value().header.region.value(), pub.ok()?1:0);
                        coordTrace("publish region=" + std::to_string(ev.value().header.region.value()) + " ok=" + std::to_string(pub.ok()) + " err=" + (pub.ok()?std::string(""):pub.error().message));
                    }
                    break;
                }
                case mef::FrameType::SELECT: {
                    auto req = mef::decodeRequirements(frame.payload);
                    if (req.ok()) {
                        mef::Authority a(c->epoch);
                        auto sel = c->fabric.select(req.value(), a);
                        if (sel.ok())
                            sendFrame(c, s, mef::FrameType::SELECT_RESP,
                                      mef::encodeSelection(sel.value()).value());
                    }
                    break;
                }
                case mef::FrameType::RESERVE: {
                    auto req = mef::decodeRequirements(frame.payload);
                    if (req.ok()) {
                        mef::Authority a(c->epoch);
                        auto rres = c->fabric.requestReservation(req.value(), a);
                        std::vector<std::uint8_t> resp;
                        resp.push_back(rres.ok() ? 1 : 0);
                        if (rres.ok()) {
                            std::uint64_t idv = rres.value().value();
                            for (int i = 0; i < 8; ++i) resp.push_back((idv >> (8*i)) & 0xFFu);
                        }
                        sendFrame(c, s, mef::FrameType::RESERVE_RESP, resp);
                    }
                    break;
                }
                case mef::FrameType::COMMIT: {
                    if (frame.payload.size() < 8) break;
                    mef::ReservationId id = mef::ReservationId(memefle64(frame.payload.data()));
                    mef::Authority a(c->epoch);
                    auto res = c->fabric.commitReservation(id, a);
                    if (res.ok())
                        sendFrame(c, s, mef::FrameType::COMMIT_RESP,
                                  mef::encodeReservation(res.value()).value());
                    break;
                }
                case mef::FrameType::RELEASE: {
                    if (frame.payload.size() < 8) break;
                    mef::ReservationId id = mef::ReservationId(memefle64(frame.payload.data()));
                    mef::Authority a(c->epoch);
                    auto res = c->fabric.releaseReservation(id, a);
                    std::vector<std::uint8_t> resp; resp.push_back(res.ok() ? 1 : 0);
                    sendFrame(c, s, mef::FrameType::RELEASE_RESP, resp);
                    break;
                }
                case mef::FrameType::INSPECT: {
                    auto snap = c->fabric.snapshot();
                    std::string text;
                    text += "epoch=" + std::to_string(snap.epoch.value()) + "\n";
                    for (const auto& rv : snap.reservations) {
                        text += "reservation " + std::to_string(rv.id.value()) +
                                " state=" + std::to_string((int)rv.state) +
                                " region=" + std::to_string(rv.region.value()) +
                                " bytes=" + std::to_string(rv.bytes) + "\n";
                    }
                    for (const auto& wd : snap.workers) {
                        text += "worker " + std::to_string(wd.id.value()) +
                                " boot=" + std::to_string(wd.boot.value()) +
                                " alive=" + (wd.alive ? "1" : "0") +
                                " handle=" + (wd.holdsProcessHandle ? "1" : "0") +
                                " pid=" + std::to_string(wd.pid) + "\n";
                    }
                    for (const auto& rg : snap.regions) {
                        text += "region " + std::to_string(rg.id.value()) +
                                " life=" + std::to_string((int)rg.lifecycle) +
                                " ev=" + std::to_string((int)rg.evidenceStatus) +
                                " free=" + std::to_string(rg.freeBytes) +
                                " reach=" + (rg.reachable?"1":"0") + "\n";
                    }
                    sendFrame(c, s, mef::FrameType::INSPECT_RESP, mef::makeStringPayload(text));
                    break;
                }
                case mef::FrameType::AUDIT: {
                    auto aud = c->fabric.audit();
                    std::string text = aud.ok() ? aud.value() : "AUDIT_ERROR";
                    sendFrame(c, s, mef::FrameType::AUDIT_RESP, mef::makeStringPayload(text));
                    break;
                }
                case mef::FrameType::SPAWN_WORKER: {
                    if (frame.payload.size() < 16) break;
                    mef::WorkerId w = mef::WorkerId(memefle64(frame.payload.data()));
                    mef::WorkerBootId b = mef::WorkerBootId(memefle64(frame.payload.data()+8));
                    HANDLE h = nullptr;
                    bool ok = spawnWorker(c, w, b, h);
                    if (ok) {
                        {
                            std::lock_guard<std::mutex> lk(c->procMtx);
                            c->procHandles[w] = h;
                        }
                        std::thread(workerMonitor, c, w, h).detach();
                    }
                    std::vector<std::uint8_t> resp; resp.push_back(ok ? 1 : 0);
                    sendFrame(c, s, mef::FrameType::SPAWN_WORKER_RESP, resp);
                    break;
                }
                case mef::FrameType::TERMINATE_WORKER: {
                    if (frame.payload.size() < 8) break;
                    mef::WorkerId w = mef::WorkerId(memefle64(frame.payload.data()));
                    HANDLE h = nullptr;
                    {
                        std::lock_guard<std::mutex> lk(c->procMtx);
                        auto it = c->procHandles.find(w);
                        if (it != c->procHandles.end()) h = it->second;
                    }
                    bool killed = false;
                    if (h != nullptr) {
#ifdef _WIN32
                        killed = ::TerminateProcess(h, 1) != 0;
#endif
                    }
                    coordTrace("TERMINATE worker=" + std::to_string(w.value()) + " hadHandle=" + std::to_string(h!=nullptr) + " killed=" + std::to_string(killed));
                    std::vector<std::uint8_t> resp; resp.push_back(killed ? 1 : 0);
                    sendFrame(c, s, mef::FrameType::TERMINATE_WORKER_RESP, resp);
                    break;
                }
                case mef::FrameType::SAVE: {
                    std::string path = std::string(frame.payload.begin(), frame.payload.end());
                    auto r = c->fabric.save(path);
                    std::vector<std::uint8_t> resp; resp.push_back(r.ok() ? 1 : 0);
                    sendFrame(c, s, mef::FrameType::SAVE_RESP, resp);
                    break;
                }
                default:
                    break;
            }
        }
    }
#ifdef _WIN32
    ::closesocket(s);
#endif
}

} // namespace

int runCoordinator(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::uint64_t port = 0;
    std::uint64_t epoch = 1;
    std::string workerExe;
    bool seed = false;
    std::string loadPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i+1 < argc) port = std::stoull(argv[++i]);
        else if (a == "--epoch" && i+1 < argc) epoch = std::stoull(argv[++i]);
        else if (a == "--worker-exe" && i+1 < argc) workerExe = argv[++i];
        else if (a == "--seed") seed = true;
        else if (a == "--load" && i+1 < argc) loadPath = argv[++i];
    }
    if (port == 0) { std::printf("coordinator: --port required\n"); return 1; }

#ifdef _WIN32
    WSADATA wsa;
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::printf("coordinator: WSAStartup failed\n"); return 1; }
#endif

    CoordPtr c;
    if (!loadPath.empty()) {
        auto rl = mef::Fabric::load(loadPath);
        if (!rl.ok()) {
            std::printf("coordinator: load failed: %s\n", rl.error().message.c_str());
            return 1;
        }
        c = std::make_shared<Coordinator>(rl.moveValue());
        epoch = c->epoch.value();
    } else {
        c = std::make_shared<Coordinator>(mef::CoordinatorEpoch(epoch));
    }
    c->port = port;
    c->workerExe = workerExe;

    if (seed && loadPath.empty()) {
        mef::Authority a(c->epoch);
        mef::ProviderDescriptor p;
        p.id = mef::ProviderId(1); p.generation = mef::ProviderGeneration(1);
        p.kind = mef::ProviderKind::SYNTHETIC; p.origin = mef::OriginClass::SYNTHETIC;
        p.name = "seed-provider";
        p.totalCapacityBytes = kSeedTotal; p.alignmentBytes = 4096; p.granularityBytes = 4096;
        p.persistence = mef::Persistence::VOLATILE;
        p.capabilities.set(mef::CapabilityKey::CPU_ACCESSIBLE, mef::CapabilityStatus::SUPPORTED);
        p.capabilities.set(mef::CapabilityKey::ACCELERATOR_ACCESSIBLE, mef::CapabilityStatus::SUPPORTED);
        p.capabilities.set(mef::CapabilityKey::COHERENT, mef::CapabilityStatus::SUPPORTED);
        p.capabilities.set(mef::CapabilityKey::BYTE_ADDRESSABLE, mef::CapabilityStatus::SUPPORTED);
        p.capabilities.set(mef::CapabilityKey::VOLATILE, mef::CapabilityStatus::SUPPORTED);
        c->fabric.registerProvider(p, a);

        mef::ExpansionDomainDescriptor dom;
        dom.id = mef::ExpansionDomainId(1); dom.generation = mef::ExpansionDomainGeneration(1);
        dom.name = "seed-domain"; dom.origin = mef::OriginClass::SYNTHETIC;
        c->fabric.registerDomain(dom, a);

        mef::RegionDescriptor r;
        r.id = mef::RegionId(1); r.generation = mef::RegionGeneration(1);
        r.provider = mef::ProviderId(1); r.domain = mef::ExpansionDomainId(1);
        r.name = "seed-region";
        r.totalCapacityBytes = kSeedTotal; r.alignmentBytes = 4096; r.granularityBytes = 4096;
        r.origin = mef::OriginClass::SYNTHETIC;
        r.staticCapabilities.set(mef::CapabilityKey::CPU_ACCESSIBLE, mef::CapabilityStatus::SUPPORTED);
        r.staticCapabilities.set(mef::CapabilityKey::ACCELERATOR_ACCESSIBLE, mef::CapabilityStatus::SUPPORTED);
        c->fabric.registerRegion(r, a);
        c->fabric.registerConsumer(mef::ConsumerId(10), mef::ConsumerGeneration(1), a);
    }

#ifdef _WIN32
    SOCK ls = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { std::printf("coordinator: socket failed\n"); return 1; }
    BOOL reuse = TRUE;
    ::setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(ls, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::printf("coordinator: bind failed %d\n", ::WSAGetLastError()); return 1;
    }
    if (::listen(ls, SOMAXCONN) == SOCKET_ERROR) { std::printf("coordinator: listen failed\n"); return 1; }
    std::printf("coordinator: listening on %llu (epoch %llu)\n", (unsigned long long)port, (unsigned long long)epoch);
#endif

    std::atomic<bool> running{true};
    while (running) {
#ifdef _WIN32
        SOCK cs = ::accept(ls, nullptr, nullptr);
        if (cs == INVALID_SOCKET) { continue; }
        std::thread(handleConnection, c, cs).detach();
#endif
    }
    return 0;
}
