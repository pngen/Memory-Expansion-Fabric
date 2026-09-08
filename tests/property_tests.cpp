#include "test_framework.hpp"
#include "scenario.hpp"
#include <string>
#include <vector>

using namespace memory_expansion_fabric;
using namespace mef_scenario;

static std::uint64_t lcg(std::uint64_t& st) {
    st = st * 6364136223846793005ull + 1442695040888963407ull;
    return st >> 33;
}

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

TEST(property_capacity_closure_and_authority) {
    for (auto seed : {1ull, 2ull, 3ull}) {
        std::uint64_t st = seed;
        Authority a(CoordinatorEpoch(1));
        Fabric f(CoordinatorEpoch(1));
        f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
        f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 1ull<<33), a);
        f.registerProvider(makeProvider(ProviderId(2), ProviderGeneration(1), ProviderKind::SYNTHETIC, 1ull<<33), a);
        f.registerRegion(makeRegion(RegionId(1), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), 1ull<<33), a);
        f.registerRegion(makeRegion(RegionId(2), RegionGeneration(1), ProviderId(2), ExpansionDomainId(1), 1ull<<33), a);
        f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a);
        for (std::uint64_t rid : {1ull, 2ull}) {
            RegionEvidence e = makeEvidence(RegionId(rid), RegionGeneration(1), ProviderId(rid), ProviderGeneration(1),
                1ull<<33, HealthState::HEALTHY, true, Locality::LOCAL, 100, 10000000000ull,
                WorkerId(), WorkerBootId(), CoordinatorEpoch(1), EvidenceId(rid), EvidenceGeneration(1), seed);
            f.publishRegionEvidence(e, a);
        }
        bool ok = true;
        std::string trace = "seed=" + std::to_string(seed) + " ";
        for (int i = 0; i < 300; ++i) {
            std::uint64_t op = lcg(st) % 6;
            std::uint64_t bytes = (1 + (lcg(st) % 1024)) * 4096;
            if (op == 0) {
                ConsumerRequirements req; req.consumer=ConsumerId(10); req.consumerGeneration=ConsumerGeneration(1); req.requiredBytes=bytes;
                auto r = f.requestReservation(req, a);
                if (r.ok()) { trace += "R; "; f.commitReservation(r.value(), a); }
                else trace += "R! ";
            } else if (op == 1) {
                auto snap = f.snapshot();
                bool did = false;
                for (const auto& rs : snap.reservations) if (rs.state == ReservationState::RESERVED) { f.commitReservation(rs.id, a); trace += "C; "; did = true; break; }
                if (!did) trace += "c ";
            } else if (op == 2) {
                auto snap = f.snapshot();
                bool did = false;
                for (const auto& rs : snap.reservations) {
                    if (rs.state == ReservationState::COMMITTED || rs.state == ReservationState::RESERVED) {
                        f.releaseReservation(rs.id, a); trace += "L; "; did = true; break;
                    }
                }
                if (!did) trace += "l ";
            } else if (op == 3) {
                f.beginDrain(RegionId(1), a); trace += "D; ";
            } else if (op == 4) {
                f.markUnavailable(RegionId(2), a); trace += "U; ";
            } else {
                f.recoverRegion(RegionId(2), a); trace += "V; ";
            }
            if (!invariantsOk(f)) { ok = false; break; }
        }
        if (!invariantsOk(f)) ok = false;
        CHECK_MSG(ok, "property seed %llu trace[%s]", (unsigned long long)seed, trace.c_str());
    }
}

MEF_MAIN()
