#include "test_framework.hpp"
#include "memory_expansion_fabric/backend.hpp"
#include "memory_expansion_fabric/fabric.hpp"
#include <string>

using namespace memory_expansion_fabric;

TEST(system_discovery_real_and_unsupported) {
    auto disc = makeDiscovery("system");
    CHECK_EQ(disc->originClass(), OriginClass::REAL);
    bool hasSystemMem = !disc->providers().empty();
    // If the machine has physical RAM, a SYSTEM_MEMORY provider is REAL.
    if (hasSystemMem) CHECK_EQ(disc->providers()[0].kind, ProviderKind::SYSTEM_MEMORY);
    std::string report = disc->capabilityReport();
    // physical expansion must be reported UNSUPPORTED (no fabrication).
    CHECK_MSG(report.find("CXL-class expansion: UNSUPPORTED") != std::string::npos, "CXL must be UNSUPPORTED");
    CHECK_MSG(report.find("remote/pooled memory: UNSUPPORTED") != std::string::npos, "remote must be UNSUPPORTED");
    CHECK_MSG(report.find("fabric-attached memory: UNSUPPORTED") != std::string::npos, "fabric must be UNSUPPORTED");
}

TEST(system_backend_feeds_fabric_real) {
    auto disc = makeDiscovery("system");
    if (disc->providers().empty()) return;   // no RAM (unlikely on this host)
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    for (auto& p : disc->providers()) CHECK(f.registerProvider(p, a).ok());
    for (auto& d : disc->domains()) CHECK(f.registerDomain(d, a).ok());
    for (auto& r : disc->regions()) CHECK(f.registerRegion(r, a).ok());
    for (auto& e : disc->initialEvidence(CoordinatorEpoch(1))) CHECK(f.publishRegionEvidence(e, a).ok());
    auto snap = f.snapshot();
    CHECK_EQ(snap.regions[0].origin, OriginClass::REAL);
    CHECK(snap.regions[0].onlineBytes > 0);
    // select: a small CPU-accessible host-memory reservation should be feasible.
    ConsumerRequirements req; req.consumer=ConsumerId(1); req.consumerGeneration=ConsumerGeneration(1);
    req.requiredBytes = 4096; req.cpuAccessible = true; req.acceleratorAccessible = false;
    auto sel = f.select(req, a);
    CHECK(sel.ok());
    CHECK_MSG(!sel.value().ranked.empty(), "host memory should be eligible for CPU");
}

TEST(synthetic_and_unsupported_backends_labeled) {
    auto sy = makeDiscovery("synthetic");
    CHECK_EQ(sy->originClass(), OriginClass::SYNTHETIC);
    CHECK(sy->capabilityReport().find("SYNTHETIC") != std::string::npos);
    auto un = makeDiscovery("unsupported");
    CHECK_EQ(un->originClass(), OriginClass::UNSUPPORTED);
    CHECK(un->providers().empty());
}

MEF_MAIN()
