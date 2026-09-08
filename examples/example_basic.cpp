// Demonstrate provider/domain/region registration and the PRESENT != ONLINE rule.
#include "example_util.hpp"
#include <cstdio>
using namespace memory_expansion_fabric;
int main() {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(ex::provider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 16*1024*1024*1024ull, Persistence::VOLATILE, true), a);
    f.registerRegion(ex::region(RegionId(1), RegionGeneration(1), ProviderId(1), 16*1024*1024*1024ull), a);
    ConsumerRequirements req; req.consumer = ConsumerId(10); req.consumerGeneration = ConsumerGeneration(1); req.requiredBytes = 4096;
    auto sel = f.select(req, a);
    std::printf("before evidence: outcome=%d ranked=%zu (PRESENT != ONLINE)\n", (int)sel.value().outcome, sel.value().ranked.size());
    int fails = (sel.ok() && sel.value().ranked.empty()) ? 0 : 1;
    // Now publish evidence; it becomes eligible.
    auto ev = ex::evidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), 16*1024*1024*1024ull, HealthState::HEALTHY, true, Locality::LOCAL, 100, 10000000000ull, CoordinatorEpoch(1));
    f.publishRegionEvidence(ev, a);
    auto sel2 = f.select(req, a);
    std::printf("after evidence: outcome=%d ranked=%zu (eligible)\n", (int)sel2.value().outcome, sel2.value().ranked.size());
    if (sel2.ok() && sel2.value().ranked.size()==1 && sel2.value().outcome==SelectionOutcome::EXPANSION_SELECTED) fails=0; else fails=1;
    std::printf("basic example: %s\n", fails?"FAIL":"PASS");
    return fails;
}
