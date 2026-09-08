// Demonstrate persistence save/load and conservative restart revalidation.
#include "example_util.hpp"
#include <cstdio>
#include <cstdlib>
using namespace memory_expansion_fabric;
int main() {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(ex::provider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 16ull*1024*1024*1024, Persistence::VOLATILE, true), a);
    f.registerRegion(ex::region(RegionId(1), RegionGeneration(1), ProviderId(1), 16ull*1024*1024*1024), a);
    f.publishRegionEvidence(ex::evidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), 16ull*1024*1024*1024, HealthState::HEALTHY, true, Locality::LOCAL, 100, 10000000000ull, CoordinatorEpoch(1)), a);
    std::string path = "mef_example_persist.bin";
    if (!f.save(path).ok()) { std::printf("save failed\n"); return 1; }
    auto g = Fabric::load(path).moveValue();
    std::printf("after restart: epoch=%llu region_life=%d evidence_status=%d (REVALIDATION_REQUIRED)\n",
                (unsigned long long)g.epoch().value(), (int)g.snapshot().regions[0].lifecycle,
                (int)g.snapshot().regions[0].evidenceStatus);
    int fails = (g.epoch().value()==2 && g.snapshot().regions[0].evidenceStatus==EvidenceStatus::UNKNOWN) ? 0 : 1;
    std::remove(path.c_str());
    std::printf("persistence example: %s\n", fails?"FAIL":"PASS");
    return fails;
}
