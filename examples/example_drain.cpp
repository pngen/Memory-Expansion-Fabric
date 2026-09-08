// Demonstrate drain / degradation / recovery.
#include "example_util.hpp"
#include <cstdio>
using namespace memory_expansion_fabric;
int main() {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(ex::provider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 16ull*1024*1024*1024, Persistence::VOLATILE, true), a);
    f.registerRegion(ex::region(RegionId(1), RegionGeneration(1), ProviderId(1), 16ull*1024*1024*1024), a);
    f.publishRegionEvidence(ex::evidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), 16ull*1024*1024*1024, HealthState::HEALTHY, true, Locality::LOCAL, 100, 10000000000ull, CoordinatorEpoch(1)), a);
    f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a);
    f.beginDrain(RegionId(1), a);
    ConsumerRequirements req; req.consumer=ConsumerId(10); req.consumerGeneration=ConsumerGeneration(1); req.requiredBytes=4096;
    auto sel = f.select(req, a);
    std::printf("during drain: ranked=%zu (drain rejects new reservations)\n", sel.value().ranked.size());
    f.completeDrain(RegionId(1), a);
    std::printf("after completeDrain: snapshot life=%d unavailable=%llu\n", (int)f.snapshot().regions[0].lifecycle, (unsigned long long)f.snapshot().regions[0].unavailableBytes);
    f.recoverRegion(RegionId(1), a);
    std::printf("after recoverRegion: snapshot life=%d evidence_status=%d\n", (int)f.snapshot().regions[0].lifecycle, (int)f.snapshot().regions[0].evidenceStatus);
    int fails = (sel.ok() && sel.value().ranked.empty()) ? 0 : 1;
    std::printf("drain example: %s\n", fails?"FAIL":"PASS");
    return fails;
}
