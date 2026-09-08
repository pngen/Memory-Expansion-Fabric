// Memory Expansion Fabric benchmark. Measures completed operations (no async
// submission, no omitted finalization). Scales through configurable sizes.
#include "memory_expansion_fabric/fabric.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace mef = memory_expansion_fabric;
using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static mef::Fabric buildRig(std::size_t n, mef::Authority& a, mef::ConsumerId& consumer) {
    mef::Fabric f(mef::CoordinatorEpoch(1));
    a = mef::Authority(mef::CoordinatorEpoch(1));
    f.registerDomain(mef::ExpansionDomainDescriptor{mef::ExpansionDomainId(1), mef::ExpansionDomainGeneration(1), "d", mef::OriginClass::SYNTHETIC, mef::FailureDomainId()}, a);
    for (std::size_t i = 0; i < n; ++i) {
        mef::ProviderDescriptor p;
        p.id = mef::ProviderId(1000 + i); p.generation = mef::ProviderGeneration(1);
        p.kind = mef::ProviderKind::SYNTHETIC; p.origin = mef::OriginClass::SYNTHETIC;
        p.name = "p" + std::to_string(i);
        p.totalCapacityBytes = 16ull*1024*1024*1024; p.alignmentBytes = 4096; p.granularityBytes = 4096;
        p.persistence = mef::Persistence::VOLATILE;
        p.capabilities.set(mef::CapabilityKey::CPU_ACCESSIBLE, mef::CapabilityStatus::SUPPORTED);
        p.capabilities.set(mef::CapabilityKey::ACCELERATOR_ACCESSIBLE, mef::CapabilityStatus::SUPPORTED);
        p.capabilities.set(mef::CapabilityKey::DIRECT_ACCESS, mef::CapabilityStatus::SUPPORTED);
        p.capabilities.set(mef::CapabilityKey::VOLATILE, mef::CapabilityStatus::SUPPORTED);
        f.registerProvider(p, a);
    }
    for (std::size_t i = 0; i < n; ++i) {
        mef::RegionDescriptor r;
        r.id = mef::RegionId(2000 + i); r.generation = mef::RegionGeneration(1);
        r.provider = mef::ProviderId(1000 + i); r.domain = mef::ExpansionDomainId(1);
        r.name = "r" + std::to_string(i);
        r.totalCapacityBytes = 16ull*1024*1024*1024; r.alignmentBytes = 4096; r.granularityBytes = 4096;
        r.origin = mef::OriginClass::SYNTHETIC;
        f.registerRegion(r, a);
    }
    for (std::size_t i = 0; i < n; ++i) {
        mef::RegionEvidence e;
        e.header.id = mef::EvidenceId(3000 + i); e.header.generation = mef::EvidenceGeneration(1);
        e.header.sourceProvider = mef::ProviderId(1000 + i); e.header.sourceProviderGeneration = mef::ProviderGeneration(1);
        e.header.region = mef::RegionId(2000 + i); e.header.regionGeneration = mef::RegionGeneration(1);
        e.header.epoch = mef::CoordinatorEpoch(1); e.header.provenance = "bench";
        e.health = mef::HealthState::HEALTHY; e.reachable = true;
        e.onlineCapacityBytes = 16ull*1024*1024*1024; e.latencyNs = 100 + i; e.bandwidthBytesPerSec = 10000000000ull;
        e.locality = mef::Locality::LOCAL; e.status = mef::EvidenceStatus::CURRENT;
        f.publishRegionEvidence(e, a);
    }
    f.registerConsumer(mef::ConsumerId(7), mef::ConsumerGeneration(1), a);
    consumer = mef::ConsumerId(7);
    return f;
}

static void runSize(std::size_t n) {
    mef::Authority a(mef::CoordinatorEpoch(1)); mef::ConsumerId consumer;
    auto t0 = Clock::now();
    mef::Fabric f = buildRig(n, a, consumer);
    double ingestMs = msSince(t0);

    mef::ConsumerRequirements req;
    req.consumer = consumer; req.consumerGeneration = mef::ConsumerGeneration(1);
    req.requiredBytes = 4096;
    std::size_t selQueries = 1000;
    t0 = Clock::now();
    std::size_t quant = 0;
    for (std::size_t i = 0; i < selQueries; ++i) {
        auto s = f.select(req, a);
        if (s.ok() && !s.value().ranked.empty()) ++quant;
    }
    double selMs = msSince(t0);

    std::size_t reserveN = n / 4 + 1;
    t0 = Clock::now();
    std::vector<mef::ReservationId> ids;
    for (std::size_t i = 0; i < reserveN; ++i) {
        auto r = f.requestReservation(req, a);
        if (r.ok()) ids.push_back(r.value());
    }
    for (auto id : ids) { f.commitReservation(id, a); }
    for (auto id : ids) { f.releaseReservation(id, a); }
    double resMs = msSince(t0);

    t0 = Clock::now();
    std::size_t snapN = 100;
    for (std::size_t i = 0; i < snapN; ++i) (void)f.snapshot();
    double snapMs = msSince(t0);

    t0 = Clock::now();
    std::string path = "mef_bench.bin";
    f.save(path);
    auto g = mef::Fabric::load(path);
    double persistMs = msSince(t0);
    std::remove(path.c_str());
    (void)g;

    std::printf("n=%zu ingest=%.1fms (%.0f ops/s) select=%.1fms (%.0f queries/s) reserve/commit/release=%.1fms (%.0f cycles/s) snapshot=%.1fms (%.0f/s) save+load=%.1fms\n",
                n, ingestMs, n*3.0/ingestMs*1000.0, selMs, selQueries/selMs*1000.0, resMs, reserveN/resMs*1000.0, snapMs, snapN/snapMs*1000.0, persistMs);
}

int main(int argc, char** argv) {
    std::vector<std::size_t> sizes = {100, 1000, 10000};
    if (argc > 1) sizes = { (std::size_t)std::stoull(argv[1]) };
    for (auto n : sizes) runSize(n);
    std::printf("benchmark complete\n");
    return 0;
}
