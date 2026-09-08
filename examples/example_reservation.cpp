// Demonstrate reserve/commit/release lifecycle and capacity closure.
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
    ConsumerRequirements req; req.consumer=ConsumerId(10); req.consumerGeneration=ConsumerGeneration(1); req.requiredBytes=4ull*1024*1024*1024;
    auto rid = f.requestReservation(req, a);
    std::printf("reserve id=%llu free=%llu\n", (unsigned long long)rid.value().value(), (unsigned long long)f.regionFree(RegionId(1)));
    auto cr = f.commitReservation(rid.value(), a);
    std::printf("commit state=%d free=%llu\n", (int)cr.value().state, (unsigned long long)f.regionFree(RegionId(1)));
    std::printf("release: %s free=%llu\n", f.releaseReservation(rid.value(), a).ok()?"ok":"failed", (unsigned long long)f.regionFree(RegionId(1)));
    int fails = (f.regionFree(RegionId(1)) == 16ull*1024*1024*1024) ? 0 : 1;
    std::printf("reservation example: %s\n", fails?"FAIL":"PASS");
    return fails;
}
