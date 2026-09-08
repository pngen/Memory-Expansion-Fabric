#pragma once
// Memory Expansion Fabric - public value types (descriptors, evidence,
// requirements, reservation, selection, snapshot).
#include "memory_expansion_fabric/id.hpp"
#include "memory_expansion_fabric/enums.hpp"
#include "memory_expansion_fabric/capability.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace memory_expansion_fabric {

// -------------------------------------------------------------------------
// Static / configured facts. Registration mutates durable structure.
// -------------------------------------------------------------------------
struct ProviderDescriptor {
    ProviderId id;
    ProviderGeneration generation;
    ProviderKind kind = ProviderKind::UNKNOWN;
    OriginClass origin = OriginClass::UNKNOWN;
    std::string name;
    FailureDomainId failureDomain;
    Addressability addressability = Addressability::UNKNOWN;
    Persistence persistence = Persistence::UNKNOWN;
    std::uint64_t totalCapacityBytes = 0;
    std::uint64_t alignmentBytes = 1;
    std::uint64_t granularityBytes = 1;
    CapabilityModel capabilities;
    bool requiresHostCopy = false;
};

struct RegionDescriptor {
    RegionId id;
    RegionGeneration generation;
    ProviderId provider;      // canonical backing provider
    ExpansionDomainId domain;
    std::string name;
    std::uint64_t totalCapacityBytes = 0;
    std::uint64_t alignmentBytes = 1;
    std::uint64_t granularityBytes = 1;
    OriginClass origin = OriginClass::UNKNOWN;
    Persistence persistence = Persistence::UNKNOWN;
    CapabilityModel staticCapabilities;
};

struct ExpansionDomainDescriptor {
    ExpansionDomainId id;
    ExpansionDomainGeneration generation;
    std::string name;
    OriginClass origin = OriginClass::UNKNOWN;
    FailureDomainId failureDomain;
};

struct PoolMember {
    RegionId region;
    RegionGeneration generation;
};

struct PoolDescriptor {
    PoolId id;
    PoolGeneration generation;
    std::string name;
    OriginClass origin = OriginClass::UNKNOWN;
    std::vector<PoolMember> members;
};

struct PolicyRule {
    std::string key;
    std::string value;
};

struct PolicyDescriptor {
    PolicyId id;
    PolicyGeneration generation;
    std::string name;
    bool allowDegraded = false;
    bool requireFreshEvidence = true;
    bool allowLocalFallback = false;
    bool expansionRequired = true;
    std::vector<PolicyRule> rules;
};

// -------------------------------------------------------------------------
// Dynamic observed evidence. Always carries source, generation, timestamp,
// provenance and authority ownership. Never silently becomes current on
// restart.
// -------------------------------------------------------------------------
struct EvidenceHeader {
    EvidenceId id;
    EvidenceGeneration generation;
    ProviderId sourceProvider;
    ProviderGeneration sourceProviderGeneration;
    RegionId region;                  // null => provider-level evidence
    RegionGeneration regionGeneration;
    WorkerId worker;
    WorkerBootId boot;
    CoordinatorEpoch epoch;
    std::uint64_t timestampMs = 0;
    std::uint64_t sequence = 0;
    std::string provenance;
    double confidence = 1.0;
};

struct RegionEvidence {
    EvidenceHeader header;
    HealthState health = HealthState::UNKNOWN;
    bool reachable = false;
    std::uint64_t onlineCapacityBytes = 0;
    std::uint64_t latencyNs = 0;             // 0 => unknown
    std::uint64_t bandwidthBytesPerSec = 0;  // 0 => unknown
    Locality locality = Locality::UNKNOWN;
    std::uint64_t nodeId = 0;
    EvidenceStatus status = EvidenceStatus::UNKNOWN;
};

struct AttachmentEvidence {
    AttachmentId id;
    AttachmentGeneration generation;
    RegionId region;
    RegionGeneration regionGeneration;
    ConsumerId consumer;              // null => generic
    AccessKind access = AccessKind::UNKNOWN;
    Locality locality = Locality::UNKNOWN;
    bool reachable = false;
    std::uint64_t latencyNs = 0;
    std::uint64_t bandwidthBytesPerSec = 0;
    WorkerId worker;
    WorkerBootId boot;
    CoordinatorEpoch epoch;
    EvidenceId evidence;
    std::uint64_t timestampMs = 0;
    EvidenceStatus status = EvidenceStatus::UNKNOWN;
};

// -------------------------------------------------------------------------
// Consumer requirements. Hard constraints must stay hard; optional
// preferences must stay optional. Nothing silently downgrades either way.
// -------------------------------------------------------------------------
struct ConsumerRequirements {
    ConsumerId consumer;
    ConsumerGeneration consumerGeneration;
    std::uint64_t requiredBytes = 0;
    std::uint64_t alignmentBytes = 1;
    bool cpuAccessible = false;
    bool acceleratorAccessible = true;
    bool directAccessRequired = false;
    bool stagingAcceptable = true;
    bool persistent = false;
    std::uint64_t maxLatencyNs = 0;   // 0 => no limit
    std::uint64_t minBandwidth = 0;   // 0 => no limit
    Locality localityPreference = Locality::UNKNOWN;
    std::vector<ProviderKind> allowedKinds;
    std::vector<ProviderKind> forbiddenKinds;
    bool failureDomainIsolationRequired = false;
    FailureDomainId forbiddenFailureDomain;
    bool freshEvidenceRequired = true;
    bool allowDegraded = false;
    bool allowLocalFallback = false;  // LOCAL_FALLBACK_SELECTED possible
    bool expansionRequired = true;    // OFFLOAD_REQUIRED semantics
    bool allowStaging = true;
    PolicyId policy;
    PoolId preferredPool;              // null => any eligible region
    double reserveHeadroomFactor = 0.0; // prefer free >= required*(1+headroom)
};

// -------------------------------------------------------------------------
// Selection / ranking results.
// -------------------------------------------------------------------------
struct RankingFactor {
    std::string name;
    double raw = 0.0;
    double normalized = 0.0;
    double weight = 0.0;
};

struct RankedCandidate {
    RegionId region;
    ProviderId provider;
    PoolId pool;            // null => none
    std::uint64_t bytes = 0;
    double score = 0.0;
    std::vector<RankingFactor> factors;
};

struct CandidateAssessment {
    RegionId region;
    ProviderId provider;
    PoolId pool;
    EligibilityReason reason = EligibilityReason::NO_ELIGIBLE_CAPACITY;
    double score = 0.0;
};

struct SelectionResult {
    SelectionId id;
    SelectionGeneration generation;
    SelectionOutcome outcome = SelectionOutcome::REJECT;
    std::vector<RankedCandidate> ranked;
    std::vector<CandidateAssessment> rejected;
    std::string tieBreakRule;
};

// -------------------------------------------------------------------------
// Reservation.
// -------------------------------------------------------------------------
struct Reservation {
    ReservationId id;
    ReservationGeneration generation;
    ConsumerId consumer;
    ConsumerGeneration consumerGeneration;
    ProviderId provider;
    ProviderGeneration providerGeneration;
    RegionId region;
    RegionGeneration regionGeneration;
    PoolId pool;
    PoolGeneration poolGeneration;
    PolicyId policy;
    PolicyGeneration policyGeneration;
    SelectionId selection;
    SelectionGeneration selectionGeneration;
    EvidenceGeneration evidenceGeneration;
    WorkerId worker;
    WorkerBootId boot;
    CoordinatorEpoch epoch;
    std::uint64_t bytes = 0;
    ReservationState state = ReservationState::REQUESTED;
    std::uint64_t createdAtMs = 0;
    std::uint64_t expiresAtMs = 0;
    std::string note;
};

// -------------------------------------------------------------------------
// Inspectable snapshots. These carry bounded, copied state for OFF-LINE
// inspection only; hot-path selection must never deep-copy these.
// -------------------------------------------------------------------------
struct ProviderSnapshot {
    ProviderId id;
    ProviderGeneration generation;
    ProviderKind kind;
    OriginClass origin;
    std::string name;
    FailureDomainId failureDomain;
    std::uint64_t totalCapacityBytes = 0;
    Lifecycle lifecycle = Lifecycle::DISCOVERED;
    std::uint64_t timestampMs = 0;
};

struct DomainSnapshot {
    ExpansionDomainId id;
    ExpansionDomainGeneration generation;
    std::string name;
    OriginClass origin;
    FailureDomainId failureDomain;
    Lifecycle lifecycle = Lifecycle::DISCOVERED;
};

struct RegionSnapshot {
    RegionId id;
    RegionGeneration generation;
    ProviderId provider;
    ExpansionDomainId domain;
    std::string name;
    std::uint64_t totalCapacityBytes = 0;
    RegionLifecycle lifecycle = RegionLifecycle::DECLARED;
    std::uint64_t onlineBytes = 0;
    std::uint64_t freeBytes = 0;
    std::uint64_t reservedBytes = 0;
    std::uint64_t committedBytes = 0;
    std::uint64_t drainingBytes = 0;
    std::uint64_t unavailableBytes = 0;
    HealthState health = HealthState::UNKNOWN;
    bool reachable = false;
    EvidenceStatus evidenceStatus = EvidenceStatus::UNKNOWN;
    std::uint64_t latencyNs = 0;
    std::uint64_t bandwidthBytesPerSec = 0;
    OriginClass origin = OriginClass::UNKNOWN;
};

struct PoolSnapshot {
    PoolId id;
    PoolGeneration generation;
    std::string name;
    OriginClass origin;
    std::vector<PoolMember> members;
    std::uint64_t totalBytes = 0;
    std::uint64_t freeBytes = 0;
};

struct ReservationSnapshot {
    ReservationId id;
    ReservationGeneration generation;
    ConsumerId consumer;
    RegionId region;
    ProviderId provider;
    std::uint64_t bytes = 0;
    ReservationState state = ReservationState::REQUESTED;
    CoordinatorEpoch epoch;
};

struct PolicySnapshot {
    PolicyId id;
    PolicyGeneration generation;
    std::string name;
    std::vector<PolicyRule> rules;
};

struct WorkerSnapshot {
    WorkerId id;
    WorkerBootId boot;
    bool registered = false;
    bool alive = false;
    bool holdsProcessHandle = false;
    std::uint64_t pid = 0;
    std::uint64_t lastSeenMs = 0;
};

struct FabricSnapshot {
    CoordinatorEpoch epoch;
    std::vector<ProviderSnapshot> providers;
    std::vector<DomainSnapshot> domains;
    std::vector<RegionSnapshot> regions;
    std::vector<PoolSnapshot> pools;
    std::vector<ReservationSnapshot> reservations;
    std::vector<PolicySnapshot> policies;
    std::vector<WorkerSnapshot> workers;
};

} // namespace memory_expansion_fabric
