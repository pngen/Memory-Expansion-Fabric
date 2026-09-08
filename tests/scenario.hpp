#pragma once
// Test scenario builders. Synthetic proof semantics only.
#include "memory_expansion_fabric/fabric.hpp"
#include "memory_expansion_fabric/types.hpp"
#include <string>

namespace mef_scenario {

using namespace memory_expansion_fabric;

inline Fabric makeFabric(CoordinatorEpoch epoch = CoordinatorEpoch(1)) {
    return Fabric(epoch);
}

inline Authority operatorAuth(CoordinatorEpoch epoch = CoordinatorEpoch(1)) {
    return Authority(epoch);
}

inline ProviderDescriptor makeProvider(ProviderId id, ProviderGeneration gen, ProviderKind kind,
                                       std::uint64_t total, OriginClass origin = OriginClass::SYNTHETIC,
                                       Persistence persist = Persistence::VOLATILE) {
    ProviderDescriptor d;
    d.id = id; d.generation = gen; d.kind = kind; d.origin = origin;
    d.name = "provider-" + std::to_string(id.value());
    d.totalCapacityBytes = total; d.alignmentBytes = 4096; d.granularityBytes = 4096;
    d.persistence = persist;
    d.capabilities.set(CapabilityKey::CPU_ACCESSIBLE, CapabilityStatus::SUPPORTED);
    d.capabilities.set(CapabilityKey::ACCELERATOR_ACCESSIBLE, CapabilityStatus::SUPPORTED);
    d.capabilities.set(CapabilityKey::COHERENT, CapabilityStatus::SUPPORTED);
    d.capabilities.set(CapabilityKey::BYTE_ADDRESSABLE, CapabilityStatus::SUPPORTED);
    if (persist == Persistence::PERSISTENT) {
        d.capabilities.set(CapabilityKey::PERSISTENT, CapabilityStatus::SUPPORTED);
    } else {
        d.capabilities.set(CapabilityKey::VOLATILE, CapabilityStatus::SUPPORTED);
    }
    return d;
}

inline RegionDescriptor makeRegion(RegionId id, RegionGeneration gen, ProviderId provider,
                                   ExpansionDomainId domain, std::uint64_t total,
                                   OriginClass origin = OriginClass::SYNTHETIC) {
    RegionDescriptor d;
    d.id = id; d.generation = gen; d.provider = provider; d.domain = domain;
    d.name = "region-" + std::to_string(id.value());
    d.totalCapacityBytes = total; d.alignmentBytes = 4096; d.granularityBytes = 4096;
    d.origin = origin;
    d.staticCapabilities.set(CapabilityKey::CPU_ACCESSIBLE, CapabilityStatus::SUPPORTED);
    d.staticCapabilities.set(CapabilityKey::ACCELERATOR_ACCESSIBLE, CapabilityStatus::SUPPORTED);
    return d;
}

inline RegionEvidence makeEvidence(RegionId region, RegionGeneration rgen, ProviderId prov,
                                   ProviderGeneration pgen, std::uint64_t online,
                                   HealthState health, bool reachable, Locality locality,
                                   std::uint64_t latencyNs, std::uint64_t bwBytesPerSec,
                                   WorkerId worker, WorkerBootId boot, CoordinatorEpoch epoch,
                                   EvidenceId eid, EvidenceGeneration egen, std::uint64_t ts) {
    RegionEvidence e;
    e.header.id = eid; e.header.generation = egen;
    e.header.sourceProvider = prov; e.header.sourceProviderGeneration = pgen;
    e.header.region = region; e.header.regionGeneration = rgen;
    e.header.worker = worker; e.header.boot = boot; e.header.epoch = epoch;
    e.header.timestampMs = ts; e.header.sequence = ts;
    e.header.provenance = "synthetic";
    e.health = health; e.reachable = reachable;
    e.onlineCapacityBytes = online;
    e.latencyNs = latencyNs; e.bandwidthBytesPerSec = bwBytesPerSec;
    e.locality = locality; e.nodeId = 1;
    e.status = EvidenceStatus::CURRENT;
    return e;
}

} // namespace mef_scenario
