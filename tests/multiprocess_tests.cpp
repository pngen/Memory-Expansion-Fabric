#include "test_framework.hpp"
#include "proto_client.hpp"
#include "process_util.hpp"
#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <windows.h>
#  undef ERROR
#endif
#include "memory_expansion_fabric/protocol.hpp"
#include <string>
#include <vector>

namespace mef = memory_expansion_fabric;

// ---- tiny text-field parser -----------------------------------------------
static long long fieldInt(const std::string& text, const std::string& key) {
    auto pos = text.find(key);
    if (pos == std::string::npos) return -1;
    pos += key.size();
    long long v = 0; bool any = false;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '=')) ++pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') { v = v*10 + (text[pos]-'0'); any = true; ++pos; }
    return any ? v : -1;
}

static bool inspect(MefClient& cli, std::string& out) {
    std::vector<std::uint8_t> resp;
    if (!cli.request(mef::FrameType::INSPECT, {}, mef::FrameType::INSPECT_RESP, resp)) return false;
    out.assign(resp.begin(), resp.end());
    return true;
}

// Poll INSPECT until the predicate holds or a very generous guard is reached.
// This is a defect guard, not a pass-on-timeout mechanism.
template <typename Pred>
static bool waitUntil(MefClient& cli, Pred p) {
    // Event-driven wait: poll INSPECT until the condition is met. The bound is
    // only a hang guard (a genuinely stuck test is a defect, not a pass).
    for (int i = 0; i < 50000; ++i) {
        std::string t;
        if (!inspect(cli, t)) return false;   // coordinator gone -> fail
        if (p(t)) return true;
        ::Sleep(2);
    }
    return false;
}

static std::uint8_t u8At(const std::vector<std::uint8_t>& v, std::size_t i) {
    return i < v.size() ? v[i] : 0;
}
static std::uint64_t u64At(const std::vector<std::uint8_t>& v, std::size_t off) {
    std::uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r |= (std::uint64_t)u8At(v, off+i) << (8*i);
    return r;
}

static std::vector<std::uint8_t> putU64(std::uint64_t v) {
    std::vector<std::uint8_t> b(8);
    for (int i = 0; i < 8; ++i) b[i] = (std::uint8_t)((v >> (8*i)) & 0xFFu);
    return b;
}

// Spawn a coordinator and connect; returns the process handle (nullptr on fail).
static HANDLE spawnCoord(std::uint16_t port, const std::string& extra) {
#ifdef _WIN32
    std::string exe = procutil::exeDir() + "\\mef_coordinator.exe";
    std::string cmd = "\"" + exe + "\" --port " + std::to_string(port) + " " + extra;
    return procutil::spawnProcess(cmd);
#else
    (void)port; (void)extra; return nullptr;
#endif
}

static mef::ConsumerRequirements makeReq(std::uint64_t bytes) {
    mef::ConsumerRequirements r;
    r.consumer = mef::ConsumerId(10);
    r.consumerGeneration = mef::ConsumerGeneration(1);
    r.requiredBytes = bytes;
    r.acceleratorAccessible = true;
    r.expansionRequired = true;
    return r;
}

TEST(worker_death_and_reincarnation) {
#ifdef _WIN32
    std::uint16_t port = procutil::pickFreePort();
    HANDLE coord = spawnCoord(port, "--seed");
    CHECK_MSG(coord != nullptr, "coordinator spawn failed");
    MefClient cli;
    CHECK_MSG(cli.connectAny("127.0.0.1", port), "client connect failed");

    // Spawn worker A (boot A1 = 10).
    std::vector<std::uint8_t> spawnPayload; spawnPayload = putU64(3); auto b = putU64(10); spawnPayload.insert(spawnPayload.end(), b.begin(), b.end());
    std::vector<std::uint8_t> resp;
    CHECK_MSG(cli.request(mef::FrameType::SPAWN_WORKER, spawnPayload, mef::FrameType::SPAWN_WORKER_RESP, resp),
              "spawn worker request");
    CHECK_MSG(u8At(resp,0) == 1, "spawn worker accepted");

    // Wait until region evidence is CURRENT (ev=1) and worker alive.
    bool ok = waitUntil(cli, [](const std::string& t){
        return fieldInt(t, "ev=") == 1 && fieldInt(t, "alive=") == 1;
    });
    CHECK_MSG(ok, "worker evidence never became current");

    // SELECT should find eligible expansion.
    auto req = makeReq(4294967296ull); // 4 GiB
    auto encReq = mef::encodeRequirements(req);
    CHECK(encReq.ok());
    std::vector<std::uint8_t> selResp;
    CHECK_MSG(cli.request(mef::FrameType::SELECT, encReq.value(), mef::FrameType::SELECT_RESP, selResp),
              "select request");
    auto sel = mef::decodeSelection(selResp);
    CHECK(sel.ok());
    CHECK_MSG(sel.value().outcome == mef::SelectionOutcome::EXPANSION_SELECTED, "select outcome");
    CHECK_EQ(sel.value().ranked.size(), 1u);

    // RESERVE.
    std::vector<std::uint8_t> resvResp;
    CHECK_MSG(cli.request(mef::FrameType::RESERVE, encReq.value(), mef::FrameType::RESERVE_RESP, resvResp),
              "reserve request");
    CHECK_MSG(u8At(resvResp,0) == 1, "reserve accepted");
    mef::ReservationId rid = mef::ReservationId(u64At(resvResp,1));

    // COMMIT.
    std::vector<std::uint8_t> commitResp;
    CHECK_MSG(cli.request(mef::FrameType::COMMIT, putU64(rid.value()), mef::FrameType::COMMIT_RESP, commitResp),
              "commit request");
    auto cr = mef::decodeReservation(commitResp);
    CHECK(cr.ok());
    CHECK_EQ(cr.value().state, mef::ReservationState::COMMITTED);

    // The coordinator owns a real process handle and the worker is alive.
    std::string t0; CHECK(inspect(cli, t0));
    CHECK_EQ(fieldInt(t0, "alive="), 1);
    CHECK_EQ(fieldInt(t0, "handle="), 1);

    // TERMINATE the worker: a real OS-process termination.
    std::vector<std::uint8_t> termResp;
    CHECK_MSG(cli.request(mef::FrameType::TERMINATE_WORKER, putU64(3), mef::FrameType::TERMINATE_WORKER_RESP, termResp),
              "terminate worker request");
    CHECK_MSG(u8At(termResp,0) == 1, "terminate accepted (real kill)");

    // Wait for death: worker not alive, evidence revalidation, reservation fenced.
    bool dead = waitUntil(cli, [](const std::string& t){
        return fieldInt(t, "alive=") == 0 &&
               fieldInt(t, "ev=") != 1 &&
               fieldInt(t, "state=") != 3;  // not COMMITTED anymore
    });
    CHECK_MSG(dead, "worker death was not detected / evidence not revalidated");

    // Re-spawn fresh worker A-prime (boot A2 = 20).
    spawnPayload = putU64(3); b = putU64(20); spawnPayload.insert(spawnPayload.end(), b.begin(), b.end());
    CHECK_MSG(cli.request(mef::FrameType::SPAWN_WORKER, spawnPayload, mef::FrameType::SPAWN_WORKER_RESP, resp),
              "respawn worker request");
    CHECK_MSG(u8At(resp,0) == 1, "respawn accepted (fresh boot)");

    bool alive2 = waitUntil(cli, [](const std::string& t){
        return fieldInt(t, "ev=") == 1 && fieldInt(t, "alive=") == 1;
    });
    CHECK_MSG(alive2, "fresh worker evidence never became current");

    // CAPACITY still exact: audit passes.
    std::vector<std::uint8_t> audResp;
    CHECK(cli.request(mef::FrameType::AUDIT, {}, mef::FrameType::AUDIT_RESP, audResp));
    std::string aud(audResp.begin(), audResp.end());
    CHECK_MSG(aud.find("FAIL") == std::string::npos, "audit after reincarnation: %s", aud.c_str());

    // Fresh selection + reservation works.
    std::vector<std::uint8_t> selResp2;
    CHECK(cli.request(mef::FrameType::SELECT, encReq.value(), mef::FrameType::SELECT_RESP, selResp2));
    auto sel2 = mef::decodeSelection(selResp2);
    CHECK(sel2.ok());
    CHECK_MSG(sel2.value().ranked.size() == 1, "selection after reincarnation");

    procutil::killProcess(coord);
    procutil::waitExit(coord);
    procutil::closeHandle(coord);
#else
    CHECK(false);
#endif
}

TEST(coordinator_restart_fencing) {
#ifdef _WIN32
    std::uint16_t port = procutil::pickFreePort();
    std::string savePath = "mef_restart_" + std::to_string(port) + ".bin";
    HANDLE coord1 = spawnCoord(port, "--seed");
    CHECK_MSG(coord1 != nullptr, "coordinator 1 spawn failed");
    MefClient cli;
    CHECK_MSG(cli.connectAny("127.0.0.1", port), "client connect failed");

    std::vector<std::uint8_t> spawnPayload = putU64(3); auto b = putU64(10); spawnPayload.insert(spawnPayload.end(), b.begin(), b.end());
    std::vector<std::uint8_t> resp;
    CHECK(cli.request(mef::FrameType::SPAWN_WORKER, spawnPayload, mef::FrameType::SPAWN_WORKER_RESP, resp));
    CHECK(waitUntil(cli, [](const std::string& t){ return fieldInt(t,"ev=")==1 && fieldInt(t,"alive=")==1; }));

    auto req = makeReq(4294967296ull);
    auto encReq = mef::encodeRequirements(req);
    std::vector<std::uint8_t> selResp;
    CHECK(cli.request(mef::FrameType::SELECT, encReq.value(), mef::FrameType::SELECT_RESP, selResp));
    std::vector<std::uint8_t> resvResp;
    CHECK(cli.request(mef::FrameType::RESERVE, encReq.value(), mef::FrameType::RESERVE_RESP, resvResp));
    CHECK(u8At(resvResp,0)==1);
    mef::ReservationId rid = mef::ReservationId(u64At(resvResp,1));
    std::vector<std::uint8_t> commitResp;
    CHECK(cli.request(mef::FrameType::COMMIT, putU64(rid.value()), mef::FrameType::COMMIT_RESP, commitResp));
    auto cr = mef::decodeReservation(commitResp);
    CHECK(cr.ok()); CHECK_EQ(cr.value().state, mef::ReservationState::COMMITTED);

    // Persist durable structure.
    std::vector<std::uint8_t> savePayload(savePath.begin(), savePath.end());
    std::vector<std::uint8_t> saveResp;
    CHECK(cli.request(mef::FrameType::SAVE, savePayload, mef::FrameType::SAVE_RESP, saveResp));
    CHECK(u8At(saveResp,0)==1);

    // Terminate coordinator #1 as a real OS process.
    procutil::killProcess(coord1);
    procutil::waitExit(coord1);
    procutil::closeHandle(coord1);

    // Start coordinator #2, loading durable state.
    HANDLE coord2 = spawnCoord(port, "--load " + savePath);
    CHECK_MSG(coord2 != nullptr, "coordinator 2 spawn failed");
    MefClient cli2;
    CHECK_MSG(cli2.connectAny("127.0.0.1", port), "client 2 connect failed");

    // Recovered dynamic evidence must not be current; reservations absent; epoch advanced.
    bool revalid = waitUntil(cli2, [](const std::string& t){
        long long ev = fieldInt(t, "ev=");
        long long epoch = fieldInt(t, "epoch=");
        // epoch=2, evidence != current(1), no committed reservation
        return epoch == 2 && ev != 1;
    });
    CHECK_MSG(revalid, "recovered evidence was not fenced / epoch not advanced");
    // reservations must be absent after restart (non-durable).
    std::string tinsp; CHECK(inspect(cli2, tinsp));
    CHECK_MSG(tinsp.find("reservation") == std::string::npos, "reservation revived after restart");

    // Fresh worker re-registers under epoch 2 and republishes.
    spawnPayload = putU64(3); b = putU64(30); spawnPayload.insert(spawnPayload.end(), b.begin(), b.end());
    CHECK(cli2.request(mef::FrameType::SPAWN_WORKER, spawnPayload, mef::FrameType::SPAWN_WORKER_RESP, resp));
    bool alive2 = waitUntil(cli2, [](const std::string& t){ return fieldInt(t,"ev=")==1 && fieldInt(t,"epoch=")==2; });
    CHECK_MSG(alive2, "fresh worker under epoch 2 did not republish");

    // SELECT works under epoch 2.
    std::vector<std::uint8_t> selResp2;
    CHECK(cli2.request(mef::FrameType::SELECT, encReq.value(), mef::FrameType::SELECT_RESP, selResp2));
    auto sel2 = mef::decodeSelection(selResp2);
    CHECK(sel2.ok());
    CHECK_MSG(sel2.value().ranked.size() == 1, "selection under epoch 2");

    procutil::killProcess(coord2);
    procutil::waitExit(coord2);
    procutil::closeHandle(coord2);
    ::remove(savePath.c_str());
#else
    CHECK(false);
#endif
}

MEF_MAIN()
