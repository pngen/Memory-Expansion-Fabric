// Demonstrate eligibility-before-ranking and deterministic ranking.
#include "example_util.hpp"
#include <cstdio>
using namespace memory_expansion_fabric;
int main() {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(ex::provider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 8ull*1024*1024*1024, Persistence::VOLATILE, true), a);
    f.registerProvider(ex::provider(ProviderId(2), ProviderGeneration(1), ProviderKind::SYNTHETIC, 8ull*1024*1024*1024, Persistence::VOLATILE, true), a);
    f.registerRegion(ex::region(RegionId(1), RegionGeneration(1), ProviderId(1), 8ull*1024*1024*1024), a);
    f.registerRegion(ex::region(RegionId(2), RegionGeneration(1), ProviderId(2), 8ull*1024*1024*1024), a);
    f.publishRegionEvidence(ex::evidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), 8ull*1024*1024*1024, HealthState::HEALTHY, true, Locality::LOCAL, 50, 20000000000ull, CoordinatorEpoch(1)), a);
    f.publishRegionEvidence(ex::evidence(RegionId(2), RegionGeneration(1), ProviderId(2), ProviderGeneration(1), 8ull*1024*1024*1024, HealthState::HEALTHY, true, Locality::REMOTE, 500, 1000000000ull, CoordinatorEpoch(1)), a);
    ConsumerRequirements req; req.consumer = ConsumerId(10); req.consumerGeneration = ConsumerGeneration(1); req.requiredBytes = 4096;
    auto sel = f.select(req, a);
    std::printf("outcome=%d top_region=%llu (local should win) score=%f\n", (int)sel.value().outcome,
                (unsigned long long)sel.value().ranked[0].region.value(), sel.value().ranked[0].score);
    for (auto& rc : sel.value().ranked[0].factors)
        std::printf("  factor %s raw=%f norm=%f w=%f\n", rc.name.c_str(), rc.raw, rc.normalized, rc.weight);
    int fails = (sel.ok() && sel.value().ranked.size()==2 && sel.value().ranked[0].region.value()==1) ? 0 : 1;
    std::printf("selection example: %s\n", fails?"FAIL":"PASS");
    return fails;
}
