// Demonstrate the synthetic multi-provider scenario via the backend.
#include "memory_expansion_fabric/backend.hpp"
#include "memory_expansion_fabric/fabric.hpp"
#include <cstdio>
#include <memory>
using namespace memory_expansion_fabric;
int main() {
    auto disc = makeDiscovery("synthetic");
    std::printf("backend=%s origin=%s\n", disc->name().c_str(), (int)disc->originClass()==0?"REAL":"SYNTHETIC");
    std::printf("summary: %s\n", disc->summary().c_str());
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    for (auto& p : disc->providers()) f.registerProvider(p, a);
    for (auto& d : disc->domains()) f.registerDomain(d, a);
    for (auto& r : disc->regions()) f.registerRegion(r, a);
    for (auto& e : disc->initialEvidence(f.epoch())) f.publishRegionEvidence(e, a);
    f.registerConsumer(ConsumerId(10), ConsumerGeneration(1), a);
    ConsumerRequirements req; req.consumer=ConsumerId(10); req.consumerGeneration=ConsumerGeneration(1); req.requiredBytes=4ull*1024*1024*1024;
    req.allowedKinds = {ProviderKind::CXL_CLASS, ProviderKind::REMOTE_MEMORY};
    auto sel = f.select(req, a);
    std::printf("synthetic select outcome=%d ranked=%zu top_region=%llu\n", (int)sel.value().outcome, sel.value().ranked.size(),
                (unsigned long long)sel.value().ranked[0].region.value());
    int fails = (sel.ok() && sel.value().ranked.size()>=1) ? 0 : 1;
    std::printf("synthetic example: %s\n", fails?"FAIL":"PASS");
    return fails;
}
