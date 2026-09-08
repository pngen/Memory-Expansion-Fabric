#include "test_framework.hpp"
#include "scenario.hpp"
#include "memory_expansion_fabric/capacity.hpp"
#include <cstdio>
#include <fstream>
#include <chrono>
#include <string>
#include <vector>

using namespace memory_expansion_fabric;
using namespace mef_scenario;

static std::string tempPath(const char* tag) {
    return std::string("mef_") + tag + "_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
}

// ---- Capacity ledger invariants -----------------------------------------
TEST(capacity_ledger_closure) {
    CapacityLedger led(1000);
    CHECK_EQ(led.total(), 1000u);
    CHECK_EQ(led.governedOnline(), 0u);
    CHECK(led.invariant());
    CHECK(led.setOnline(1000));
    CHECK_EQ(led.free(), 1000u);
    CHECK(led.reserve(300));
    CHECK(led.commit(200));
    CHECK(led.beginDrain(100));
    CHECK(led.markUnavailable(50));
    // free must be 1000 - 300 - 100 - 50 = 550 ... but committed 200 came from reserved
    CHECK_EQ(led.free(), 550u);
    CHECK_EQ(led.reserved(), 100u);
    CHECK_EQ(led.committed(), 200u);
    CHECK_EQ(led.draining(), 100u);
    CHECK_EQ(led.unavailable(), 50u);
    CHECK(led.invariant());
    CHECK(led.release(100));      // reserved back to free
    CHECK_EQ(led.free(), 650u);
    CHECK_EQ(led.reserved(), 0u);
    CHECK(led.invariant());
    CHECK(led.complete(200));     // committed back to free
    CHECK_EQ(led.free(), 850u);
    CHECK(led.invariant());
    CHECK(!led.reserve(1000));    // underflow (free=850)
    CHECK_EQ(led.free(), 850u);   // unchanged on failure
    CHECK(led.setOnline(1000));
    CHECK(!led.setOnline(1001));  // online > total rejected
    CHECK(led.setOnline(500));    // shrink: 500 >= obligations (free+reserved+committed+draining+unavailable=1000-? wait)
}

TEST(capacity_ledger_no_double) {
    CapacityLedger led(100);
    CHECK(led.setOnline(100));
    CHECK(led.reserve(60));
    CHECK(!led.reserve(50));   // free=40
    CHECK(!led.commit(70));    // reserved=60
    CHECK(led.commit(40));
    CHECK_EQ(led.reserved(), 20u);
    CHECK_EQ(led.committed(), 40u);
    CHECK(led.invariant());
}

// ---- End-to-end happy path ------------------------------------------------
static RegionDescriptor setupRegion(Fabric& f, const Authority& a) {
    auto pid = ProviderId(1);
    auto pgen = ProviderGeneration(1);
    auto did = ExpansionDomainId(1);
    auto pg = ProviderGeneration(1);
    f.registerDomain(ExpansionDomainDescriptor{did, ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a).ok();
    CHECK(f.registerProvider(makeProvider(pid, pgen, ProviderKind::SYNTHETIC, 4096), a).ok());
    RegionDescriptor rd = makeRegion(RegionId(1), RegionGeneration(1), pid, did, 4096);
    CHECK(f.registerRegion(rd, a).ok());
    return rd;
}

inline void publishOnline(Fabric& f, RegionId region, std::uint64_t online, const Authority& a,
                          HealthState h = HealthState::HEALTHY, bool reach = true) {
    RegionEvidence ev = makeEvidence(region, RegionGeneration(1), ProviderId(1), ProviderGeneration(1),
        online, h, reach, Locality::LOCAL, 100, 1000000000,
        WorkerId(), WorkerBootId(), a.epoch, EvidenceId(1), EvidenceGeneration(1), 1000);
    CHECK(f.publishRegionEvidence(ev, a).ok());
}

TEST(end_to_end_reserve_commit_release) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a);
    f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a).ok();

    ConsumerRequirements req;
    req.consumer = ConsumerId(10); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 1024; req.acceleratorAccessible = true; req.expansionRequired = true;
    auto sel = f.select(req, a);
    CHECK(sel.ok());
    CHECK(sel.value().outcome == SelectionOutcome::EXPANSION_SELECTED);
    CHECK_EQ(sel.value().ranked.size(), 1u);

    auto rres = f.requestReservation(req, a);
    CHECK(rres.ok());
    auto rid = rres.value();
    CHECK(f.workerAlive(WorkerId(99), WorkerBootId(99)) == false);

    auto cr = f.commitReservation(rid, a);
    CHECK(cr.ok());
    CHECK(cr.value().state == ReservationState::COMMITTED);
    CHECK_EQ(cr.value().bytes, 1024u);
    CHECK_EQ(f.regionFree(RegionId(1)), 4096u - 1024u);

    CHECK(f.releaseReservation(rid, a).ok());
    CHECK_EQ(f.regionFree(RegionId(1)), 4096u);

    // double release rejected
    CHECK(!f.releaseReservation(rid, a).ok());
}

// ---- PRESENT != ONLINE ---------------------------------------------------
TEST(present_not_online) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);   // registered, no evidence published
    ConsumerRequirements req; req.consumer = ConsumerId(1); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 64;
    auto sel = f.select(req, a);
    CHECK(sel.ok());
    CHECK(sel.value().outcome == SelectionOutcome::REVALIDATION_REQUIRED);
    CHECK(sel.value().ranked.empty());
}

// ---- ONLINE != REACHABLE --------------------------------------------------
TEST(online_not_reachable) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a, HealthState::HEALTHY, false); // not reachable
    ConsumerRequirements req; req.consumer = ConsumerId(1); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 64;
    auto sel = f.select(req, a);
    CHECK(sel.ok());
    CHECK(sel.value().ranked.empty());
    CHECK_EQ(sel.value().rejected[0].reason, EligibilityReason::UNREACHABLE);
}

// ---- REACHABLE != ELIGIBLE (degraded not allowed) -------------------------
TEST(reachable_not_eligible_degraded) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a, HealthState::DEGRADED, true);
    ConsumerRequirements req; req.consumer = ConsumerId(1); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 64; req.allowDegraded = false;
    auto sel = f.select(req, a);
    CHECK(sel.ok());
    CHECK_EQ(sel.value().rejected[0].reason, EligibilityReason::DEGRADED_NOT_ALLOWED);
    req.allowDegraded = true;
    auto sel2 = f.select(req, a);
    CHECK(sel2.ok());
    CHECK(sel2.value().ranked.size() == 1);
}

// ---- FREE != RESERVABLE (drain) -------------------------------------------
TEST(free_not_reservable_drain) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a);
    CHECK(f.beginDrain(RegionId(1), a).ok());
    ConsumerRequirements req; req.consumer = ConsumerId(1); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 64;
    auto sel = f.select(req, a);
    CHECK(sel.ok());
    CHECK(sel.value().ranked.empty());   // draining free must not be reservable
}

// ---- RESERVABLE != COMMITTED (stale commit after provider reincarnation) --
TEST(reservable_not_committed_stale) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a);
    f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a).ok();
    ConsumerRequirements req; req.consumer = ConsumerId(10); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 512;
    auto rid = f.requestReservation(req, a).value();
    // Reincarnate provider gen1 -> gen2; reservation must be fenced.
    auto d2 = makeProvider(ProviderId(1), ProviderGeneration(2), ProviderKind::SYNTHETIC, 4096);
    CHECK(f.registerProvider(d2, a).ok());
    CHECK(!f.commitReservation(rid, a).ok());  // stale reservation rejected
    CHECK(f.releaseReservation(rid, a).ok() == false); // already fenced/not releasable
}

// ---- double reservation prevented -----------------------------------------
TEST(no_double_reservation) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a);
    f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a).ok();
    ConsumerRequirements req; req.consumer = ConsumerId(10); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 3000;
    CHECK(f.requestReservation(req, a).ok());
    CHECK(!f.requestReservation(req, a).ok()); // only 1096 left, need 3000
    auto r2 = f.requestReservation(req, a);
    CHECK(!r2.ok());
}

// ---- stale generation evidence rejected ------------------------------------
TEST(stale_region_generation_evidence) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    auto ev = makeEvidence(RegionId(1), RegionGeneration(2) /* wrong */, ProviderId(1), ProviderGeneration(1),
        4096, HealthState::HEALTHY, true, Locality::LOCAL, 100, 100, WorkerId(), WorkerBootId(),
        a.epoch, EvidenceId(1), EvidenceGeneration(1), 1000);
    CHECK(!f.publishRegionEvidence(ev, a).ok());
}

// ---- worker authority + stale boot replay ----------------------------------
TEST(worker_authority_and_stale_boot) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    WorkerId w = WorkerId(7); WorkerBootId b1 = WorkerBootId(1);
    CHECK(f.registerWorker(w, b1, CoordinatorEpoch(1), 12345, a).ok());
    CHECK(f.workerAlive(w, b1));
    Authority wa(CoordinatorEpoch(1), w, b1);
    RegionEvidence ev = makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1),
        4096, HealthState::HEALTHY, true, Locality::LOCAL, 100, 100, w, b1, CoordinatorEpoch(1),
        EvidenceId(1), EvidenceGeneration(1), 1000);
    CHECK(f.publishRegionEvidence(ev, wa).ok());
    // stale boot replay (b1 after death)
    CHECK(f.markWorkerDead(w, a).ok());
    CHECK(!f.workerAlive(w, b1));
    WorkerBootId b2 = WorkerBootId(2);
    Authority stale(CoordinatorEpoch(1), w, b1);
    RegionEvidence staleEv = makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1),
        4096, HealthState::HEALTHY, true, Locality::LOCAL, 100, 100, w, b1, CoordinatorEpoch(1),
        EvidenceId(2), EvidenceGeneration(2), 2000);
    CHECK(!f.publishRegionEvidence(staleEv, stale).ok());   // stale boot rejected
    // reincarnate with fresh boot
    CHECK(f.registerWorker(w, b2, CoordinatorEpoch(1), 12345, a).ok());
    Authority wa2(CoordinatorEpoch(1), w, b2);
    RegionEvidence ev2 = makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1),
        4096, HealthState::HEALTHY, true, Locality::LOCAL, 100, 100, w, b2, CoordinatorEpoch(1),
        EvidenceId(3), EvidenceGeneration(3), 3000);
    CHECK(f.publishRegionEvidence(ev2, wa2).ok());
}

// ---- deterministic ranking --------------------------------------------------
TEST(deterministic_ranking_permutation) {
    Authority a = operatorAuth();
    // Two providers/regions: A (local lower latency), B (remote higher latency).
    // Regardless of registration order, A must rank first.
    for (int order = 0; order < 2; ++order) {
        Fabric f = makeFabric();
        f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
        auto pa = makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 8192);
        auto pb = makeProvider(ProviderId(2), ProviderGeneration(1), ProviderKind::SYNTHETIC, 8192);
        if (order == 0) { f.registerProvider(pa, a); f.registerProvider(pb, a); }
        else { f.registerProvider(pb, a); f.registerProvider(pa, a); }
        f.registerRegion(makeRegion(RegionId(1), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), 8192), a);
        f.registerRegion(makeRegion(RegionId(2), RegionGeneration(1), ProviderId(2), ExpansionDomainId(1), 8192), a);
        auto ea = makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), 8192,
            HealthState::HEALTHY, true, Locality::LOCAL, 50, 1000, WorkerId(), WorkerBootId(), a.epoch,
            EvidenceId(1), EvidenceGeneration(1), 1);
        auto eb = makeEvidence(RegionId(2), RegionGeneration(1), ProviderId(2), ProviderGeneration(1), 8192,
            HealthState::HEALTHY, true, Locality::REMOTE, 400, 500, WorkerId(), WorkerBootId(), a.epoch,
            EvidenceId(2), EvidenceGeneration(1), 1);
        f.publishRegionEvidence(ea, a);
        f.publishRegionEvidence(eb, a);
        ConsumerRequirements req; req.consumer = ConsumerId(1); req.consumerGeneration = ConsumerGeneration(1);
        req.requiredBytes = 512;
        auto sel = f.select(req, a);
        CHECK(sel.ok());
        CHECK_EQ(sel.value().ranked[0].region.value(), 1u);  // local should win
        CHECK_EQ(sel.value().ranked.size(), 2u);
    }
}

// ---- eligibility before ranking --------------------------------------------
TEST(eligibility_before_ranking) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    // Provider 1 is huge but wrong kind; Provider 2 smaller but allowed.
    auto p1 = makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::REMOTE_MEMORY, 100000);
    auto p2 = makeProvider(ProviderId(2), ProviderGeneration(1), ProviderKind::SYNTHETIC, 4096);
    f.registerProvider(p1, a); f.registerProvider(p2, a);
    f.registerRegion(makeRegion(RegionId(1), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), 100000), a);
    f.registerRegion(makeRegion(RegionId(2), RegionGeneration(1), ProviderId(2), ExpansionDomainId(1), 4096), a);
    auto e1 = makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), 100000,
        HealthState::HEALTHY, true, Locality::LOCAL, 10, 10000, WorkerId(), WorkerBootId(), a.epoch, EvidenceId(1), EvidenceGeneration(1), 1);
    auto e2 = makeEvidence(RegionId(2), RegionGeneration(1), ProviderId(2), ProviderGeneration(1), 4096,
        HealthState::HEALTHY, true, Locality::LOCAL, 100, 1000, WorkerId(), WorkerBootId(), a.epoch, EvidenceId(2), EvidenceGeneration(1), 1);
    f.publishRegionEvidence(e1, a); f.publishRegionEvidence(e2, a);
    ConsumerRequirements req; req.consumer = ConsumerId(1); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 512;
    req.allowedKinds = {ProviderKind::SYNTHETIC};   // REMOTE_MEMORY ineligible
    auto sel = f.select(req, a);
    CHECK(sel.ok());
    CHECK_EQ(sel.value().ranked.size(), 1u);
    CHECK_EQ(sel.value().ranked[0].region.value(), 2u);   // only eligible one wins
    CHECK_EQ(sel.value().rejected.size(), 1u);
    CHECK_EQ(sel.value().rejected[0].reason, EligibilityReason::INCOMPATIBLE);
}

// ---- pooling ----------------------------------------------------------------
TEST(pooling_no_double_count) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 8192), a);
    f.registerRegion(makeRegion(RegionId(1), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), 8192), a);
    f.registerPool(PoolDescriptor{PoolId(1), PoolGeneration(1), "pool", OriginClass::SYNTHETIC, {}}, a);
    CHECK(f.addPoolMember(PoolId(1), PoolMember{RegionId(1), RegionGeneration(1)}, a).ok());
    publishOnline(f, RegionId(1), 8192, a);

    // stale member generation rejected
    CHECK(!f.addPoolMember(PoolId(1), PoolMember{RegionId(1), RegionGeneration(2)}, a).ok());

    ConsumerRequirements req; req.consumer = ConsumerId(1); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 512; req.preferredPool = PoolId(1);
    auto sel = f.select(req, a);
    CHECK(sel.ok());
    CHECK_EQ(sel.value().ranked[0].region.value(), 1u);

    // Pool capacity view must not exceed the backing region authority.
    auto snap = f.snapshot();
    CHECK_EQ(snap.pools[0].totalBytes, 8192u);
    CHECK_EQ(snap.pools[0].freeBytes, 8192u);

    // Reserve through the pool; backing region tracks it exactly once.
    f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a).ok();
    req.consumer = ConsumerId(10); req.consumerGeneration = ConsumerGeneration(1);
    CHECK(f.requestReservation(req, a).ok());
    CHECK_EQ(f.regionFree(RegionId(1)), 8192u - 512u);
}

// ---- persistence save/load revalidation --------------------------------------
TEST(persistence_save_load_revalidation) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a);
    f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a).ok();
    ConsumerRequirements req; req.consumer = ConsumerId(10); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 512;
    auto rid = f.requestReservation(req, a);
    CHECK(rid.ok());
    CHECK(f.commitReservation(rid.value(), a).ok());

    std::string path = tempPath("persist");
    CHECK(f.save(path).ok());

    auto rl = Fabric::load(path);
    CHECK(rl.ok());
    Fabric g = rl.moveValue();
    CHECK_EQ(g.epoch().value(), 2u);   // next epoch
    auto snap = g.snapshot();
    // Recovered dynamic evidence must NOT be current.
    CHECK_EQ(snap.regions[0].lifecycle, RegionLifecycle::REVALIDATION_REQUIRED);
    CHECK_EQ(snap.regions[0].evidenceStatus, EvidenceStatus::UNKNOWN);
    // Reservations are non-durable: absent after restart.
    CHECK_EQ(snap.reservations.size(), 0u);
    CHECK(snap.regions[0].freeBytes == 0u);

    // Must revalidate before selection returns eligibility.
    Authority a2(CoordinatorEpoch(2));
    ConsumerRequirements req2 = req;
    req2.consumer = ConsumerId(10);
    auto sel2 = g.select(req2, a2);
    CHECK(sel2.ok());
    CHECK(sel2.value().ranked.empty());
    CHECK(sel2.value().outcome == SelectionOutcome::REVALIDATION_REQUIRED);

    // Republish fresh evidence under epoch 2.
    publishOnline(g, RegionId(1), 4096, a2);
    auto sel3 = g.select(req2, a2);
    CHECK(sel3.ok());
    CHECK(sel3.value().ranked.size() == 1);

    std::remove(path.c_str());
}

// ---- persistence corruption/truncation rejected ------------------------------
TEST(persistence_corruption_rejected) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a);
    std::string path = tempPath("corrupt");
    CHECK(f.save(path).ok());

    {
        // corrupt one byte in payload
        std::ifstream fin(path, std::ios::binary);
        std::vector<char> b((std::istreambuf_iterator<char>(fin)), {});
        fin.close();
        b[b.size() / 2] ^= 0x41;
        std::ofstream fout(path, std::ios::binary | std::ios::trunc);
        fout.write(b.data(), (std::streamsize)b.size());
        fout.close();
        CHECK(!Fabric::load(path).ok());
    }
    // truncation
    {
        std::ifstream fin(path, std::ios::binary);
        std::vector<char> b((std::istreambuf_iterator<char>(fin)), {});
        fin.close();
        b.resize(b.size() / 2);
        std::ofstream fout(path, std::ios::binary | std::ios::trunc);
        fout.write(b.data(), (std::streamsize)b.size());
        fout.close();
        CHECK(!Fabric::load(path).ok());
    }
    // garbage magic
    {
        std::ofstream fout(path, std::ios::binary | std::ios::trunc);
        fout << "not-a-mef-file" << std::endl;
        fout.close();
        CHECK(!Fabric::load(path).ok());
    }
    std::remove(path.c_str());
}

// ---- stale epoch replay rejected -----------------------------------------------
TEST(stale_epoch_replay_rejected) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a);
    f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a).ok();
    ConsumerRequirements req; req.consumer = ConsumerId(10); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 64;
    auto rid = f.requestReservation(req, a).value();
    // advance epoch
    f.setEpoch(CoordinatorEpoch(2));
    // old-epoch authority rejected
    CHECK(!f.requestReservation(req, a).ok());
    // old-epoch commit of a pre-advance reservation rejected
    CHECK(!f.commitReservation(rid, a).ok());
}

// ---- audit passes ----------------------------------------------------------------
TEST(audit_passes) {
    Authority a = operatorAuth();
    Fabric f = makeFabric();
    setupRegion(f, a);
    publishOnline(f, RegionId(1), 4096, a);
    f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a).ok();
    ConsumerRequirements req; req.consumer = ConsumerId(10); req.consumerGeneration = ConsumerGeneration(1);
    req.requiredBytes = 1024;
    auto rid = f.requestReservation(req, a).value();
    f.commitReservation(rid, a);
    auto aud = f.audit();
    CHECK(aud.ok());
    CHECK(aud.value().find("FAIL") == std::string::npos);
}

MEF_MAIN()
