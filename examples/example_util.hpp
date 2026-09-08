#pragma once
// Shared example helpers (synthetic proof scenarios).
#include "memory_expansion_fabric/fabric.hpp"
#include <string>

namespace ex {
using namespace memory_expansion_fabric;

inline ProviderDescriptor provider(ProviderId id, ProviderGeneration g, ProviderKind kind,
                                   std::uint64_t total, Persistence persist, bool direct) {
    ProviderDescriptor d;
    d.id = id; d.generation = g; d.kind = kind; d.origin = OriginClass::SYNTHETIC;
    d.name = "provider-" + std::to_string(id.value());
    d.totalCapacityBytes = total; d.alignmentBytes = 4096; d.granularityBytes = 4096;
    d.persistence = persist;
    d.capabilities.set(CapabilityKey::CPU_ACCESSIBLE, CapabilityStatus::SUPPORTED);
    d.capabilities.set(CapabilityKey::ACCELERATOR_ACCESSIBLE, CapabilityStatus::SUPPORTED);
    d.capabilities.set(CapabilityKey::COHERENT, CapabilityStatus::SUPPORTED);
    if (persist == Persistence::PERSISTENT)
        d.capabilities.set(CapabilityKey::PERSISTENT, CapabilityStatus::SUPPORTED);
    else
        d.capabilities.set(CapabilityKey::VOLATILE, CapabilityStatus::SUPPORTED);
    if (direct)
        d.capabilities.set(CapabilityKey::DIRECT_ACCESS, CapabilityStatus::SUPPORTED);
    else
        d.capabilities.set(CapabilityKey::STAGING_REQUIRED, CapabilityStatus::SUPPORTED);
    return d;
}
inline RegionDescriptor region(RegionId id, RegionGeneration g, ProviderId p, std::uint64_t total) {
    RegionDescriptor r;
    r.id = id; r.generation = g; r.provider = p; r.domain = ExpansionDomainId(1);
    r.name = "region-" + std::to_string(id.value());
    r.totalCapacityBytes = total; r.alignmentBytes = 4096; r.granularityBytes = 4096;
    r.origin = OriginClass::SYNTHETIC;
    return r;
}
inline RegionEvidence evidence(RegionId region, RegionGeneration rgen, ProviderId p, ProviderGeneration pgen,
                               std::uint64_t online, HealthState h, bool reach, Locality loc,
                               std::uint64_t lat, std::uint64_t bw, CoordinatorEpoch ep) {
    RegionEvidence e;
    e.header.id = EvidenceId(1); e.header.generation = EvidenceGeneration(1);
    e.header.sourceProvider = p; e.header.sourceProviderGeneration = pgen;
    e.header.region = region; e.header.regionGeneration = rgen; e.header.epoch = ep;
    e.header.provenance = "synthetic";
    e.health = h; e.reachable = reach; e.onlineCapacityBytes = online;
    e.latencyNs = lat; e.bandwidthBytesPerSec = bw; e.locality = loc; e.status = EvidenceStatus::CURRENT;
    return e;
}
}
