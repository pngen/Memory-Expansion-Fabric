#pragma once
// Memory Expansion Fabric - provider/back-end discovery interface.
//
// A backend inspects some source (real system, synthetic scenario, or absence
// of hardware) and produces: static providers/domains/regions, optional initial
// dynamic evidence, a reality classification, and an honest human summary.
//
// NATURE OF CLAIMS: a backend never fabricates hardware. The system backend
// reports physical expansion as UNSUPPORTED when none is present; the synthetic
// backend labels everything SYNTHETIC.
#include "memory_expansion_fabric/id.hpp"
#include "memory_expansion_fabric/enums.hpp"
#include "memory_expansion_fabric/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace memory_expansion_fabric {

class Discovery {
public:
    virtual ~Discovery() = default;
    virtual std::string name() const = 0;
    virtual std::string summary() const = 0;
    virtual OriginClass originClass() const = 0;

    virtual std::vector<ProviderDescriptor> providers() const = 0;
    virtual std::vector<ExpansionDomainDescriptor> domains() const = 0;
    virtual std::vector<RegionDescriptor> regions() const = 0;
    // Optional dynamic evidence a backend seeds (e.g. host-memory online).
    virtual std::vector<RegionEvidence> initialEvidence(CoordinatorEpoch epoch) const {
        (void)epoch; return {};
    }

    // A concise REAL / SYNTHETIC / UNSUPPORTED capability matrix for display.
    virtual std::string capabilityReport() const { return ""; }
};

// Backend kind: "system", "synthetic", "unsupported".
std::unique_ptr<Discovery> makeDiscovery(const std::string& kind);

} // namespace memory_expansion_fabric
