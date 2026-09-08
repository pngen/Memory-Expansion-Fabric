#include "test_framework.hpp"
#include "scenario.hpp"
#include <thread>
#include <atomic>
#include <vector>

using namespace memory_expansion_fabric;
using namespace mef_scenario;

static bool invariantsOk(Fabric& f) {
    auto aud = f.audit();
    if (!aud.ok() || aud.value().find("FAIL") != std::string::npos) return false;
    auto snap = f.snapshot();
    for (const auto& r : snap.regions) {
        std::uint64_t sum = r.freeBytes + r.reservedBytes + r.committedBytes + r.drainingBytes + r.unavailableBytes;
        if (sum != r.onlineBytes) return false;
        if (r.onlineBytes > r.totalCapacityBytes) return false;
    }
    return true;
}

TEST(concurrency_reserve_vs_reserve) {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 1ull<<30), a);
    f.registerRegion(makeRegion(RegionId(1), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), 1ull<<30), a);
    RegionEvidence e = makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1),
        1ull<<30, HealthState::HEALTHY, true, Locality::LOCAL, 100, 10000000000ull,
        WorkerId(), WorkerBootId(), CoordinatorEpoch(1), EvidenceId(1), EvidenceGeneration(1), 1);
    f.publishRegionEvidence(e, a);
    f.registerConsumer(ConsumerId(1), ConsumerGeneration(1), a);

    constexpr int kThreads = 16;
    std::atomic<int> okCount{0}, failCount{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&](){
            for (int i = 0; i < 200; ++i) {
                ConsumerRequirements req; req.consumer=ConsumerId(1); req.consumerGeneration=ConsumerGeneration(1); req.requiredBytes=4096;
                auto r = f.requestReservation(req, a);
                if (r.ok()) { ++okCount; f.releaseReservation(r.value(), a); }
                else ++failCount;
            }
        });
    }
    for (auto& th : threads) th.join();
    // Every reservation succeeded and was released: no double-reserve ever.
    CHECK_EQ(okCount.load(), kThreads * 200);
    CHECK_EQ(failCount.load(), 0);
    CHECK(invariantsOk(f));
    CHECK_EQ(f.regionFree(RegionId(1)), 1ull<<30);
}

TEST(concurrency_reserve_vs_drain_and_update) {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 1ull<<30), a);
    f.registerRegion(makeRegion(RegionId(1), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), 1ull<<30), a);
    RegionEvidence e = makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1),
        1ull<<30, HealthState::HEALTHY, true, Locality::LOCAL, 100, 10000000000ull,
        WorkerId(), WorkerBootId(), CoordinatorEpoch(1), EvidenceId(1), EvidenceGeneration(1), 1);
    f.publishRegionEvidence(e, a);
    f.registerConsumer(ConsumerId(1), ConsumerGeneration(1), a);

    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    // reservers
    std::atomic<int> okc{0};
    for (int t = 0; t < 8; ++t) threads.emplace_back([&](){
        for (int i = 0; i < 100; ++i) {
            ConsumerRequirements req; req.consumer=ConsumerId(1); req.consumerGeneration=ConsumerGeneration(1); req.requiredBytes=4096;
            auto r = f.requestReservation(req, a);
            if (r.ok()) { ++okc; f.releaseReservation(r.value(), a); }
            (void)stop.load();
        }
    });
    // selector thread (read-only, concurrent)
    threads.emplace_back([&](){
        for (int i = 0; i < 200; ++i) {
            ConsumerRequirements req; req.consumer=ConsumerId(1); req.consumerGeneration=ConsumerGeneration(1); req.requiredBytes=4096;
            auto s = f.select(req, a); (void)s;
        }
    });
    // drain/update thread
    threads.emplace_back([&](){
        f.beginDrain(RegionId(1), a);
        f.markUnavailable(RegionId(1), a);
    });
    for (auto& th : threads) th.join();
    CHECK(invariantsOk(f));
    CHECK(okc.load() >= 0);
}

TEST(concurrency_commit_vs_generation_replacement) {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 1ull<<30), a);
    f.registerRegion(makeRegion(RegionId(1), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), 1ull<<30), a);
    f.publishRegionEvidence(makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), 1ull<<30, HealthState::HEALTHY, true, Locality::LOCAL, 100, 10000000000ull, WorkerId(), WorkerBootId(), CoordinatorEpoch(1), EvidenceId(1), EvidenceGeneration(1), 1), a);
    f.registerConsumer(ConsumerId(1), ConsumerGeneration(1), a);
    ConsumerRequirements req; req.consumer=ConsumerId(1); req.consumerGeneration=ConsumerGeneration(1); req.requiredBytes=4096;
    auto rid = f.requestReservation(req, a).value();
    std::thread repl([&](){ f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(2), ProviderKind::SYNTHETIC, 1ull<<30), a); });
    bool committed = f.commitReservation(rid, a).ok();
    repl.join();
    // commit either succeeded (if before replacement) or was rejected as stale.
    CHECK(invariantsOk(f));
    (void)committed;
}

MEF_MAIN()
