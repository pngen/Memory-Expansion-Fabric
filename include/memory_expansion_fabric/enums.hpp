#pragma once
// Memory Expansion Fabric - enumerations.
#include <cstdint>

namespace memory_expansion_fabric {

// Classification of an expansion source. This is a *generic* classification;
// presence of a value here never implies that physical hardware exists.
enum class ProviderKind : std::uint8_t {
    SYSTEM_MEMORY,         // ordinary host/system memory modelled as a domain
    HOST_EXPANSION,        // host-adjacent expansion
    CXL_CLASS,             // CXL-class memory (interconnect-generic)
    REMOTE_MEMORY,         // remote-memory / pooled appliance
    FABRIC_ATTACHED,       // fabric-attached memory
    ACCELERATOR_ADJACENT,  // accelerator-adjacent expansion
    SOFTWARE_DEFINED,      // software-defined expanded-memory tier
    SYNTHETIC,             // synthetic/proof-only expansion domain
    UNKNOWN
};

// Reality classification for inspectable output and labelling. Never blur
// synthetic proof domains with real hardware.
enum class OriginClass : std::uint8_t {
    REAL,
    SYNTHETIC,
    UNSUPPORTED,
    UNKNOWN
};

enum class CapabilityStatus : std::uint8_t {
    SUPPORTED,
    UNSUPPORTED,
    UNKNOWN,
    REVALIDATION_REQUIRED
};

// The capability keys the runtime understands. Other keys are possible via
// the raw note mechanism but these are the ones the runtime reasons about.
enum class CapabilityKey : std::uint8_t {
    CPU_ACCESSIBLE,
    ACCELERATOR_ACCESSIBLE,
    PERSISTENT,
    VOLATILE,
    BYTE_ADDRESSABLE,
    BLOCK_ORIENTED,
    COHERENT,
    NON_COHERENT,
    DIRECT_ACCESS,
    STAGING_REQUIRED,
    HOST_MEDIATION_REQUIRED,
    MIGRATION_SUPPORTED,
    DRAIN_SUPPORTED,
    LIVE_WITHDRAWAL_SUPPORTED,
    HOT_ADD_REMOVE_OBSERVABLE,
    FAILURE_DOMAIN_ISOLATION_KNOWN,
    PERFORMANCE_EVIDENCE_CURRENT
};

// Provider / domain lifecycle.
enum class Lifecycle : std::uint8_t {
    DISCOVERED,
    REGISTERED,
    PROBING,
    ONLINE,
    DEGRADED,
    DRAINING,
    OFFLINE,
    REVALIDATION_REQUIRED,
    FAILED,
    RETIRED
};

// Region lifecycle.
enum class RegionLifecycle : std::uint8_t {
    DECLARED,
    ONLINE,
    DEGRADED,
    DRAINING,
    UNAVAILABLE,
    REVALIDATION_REQUIRED,
    RETIRED
};

// Reservation lifecycle.
enum class ReservationState : std::uint8_t {
    REQUESTED,
    EVALUATED,
    RESERVED,
    COMMITTED,
    ACTIVE,
    RELEASED,
    REJECTED,
    EXPIRED,
    FENCED,
    REVALIDATION_REQUIRED,
    CANCELLED
};

// Eligibility is evaluated BEFORE ranking. Every ineligible candidate returns
// a typed reason; a candidate is never silently dropped behind a score.
enum class EligibilityReason : std::uint8_t {
    ELIGIBLE,
    INSUFFICIENT_CAPACITY,
    OFFLINE,
    DEGRADED_NOT_ALLOWED,
    UNREACHABLE,
    CAPABILITY_UNSUPPORTED,
    CAPABILITY_UNKNOWN,
    INCOMPATIBLE,
    TOO_REMOTE,
    LATENCY_LIMIT_EXCEEDED,
    BANDWIDTH_INSUFFICIENT,
    PERSISTENCE_REQUIRED,
    STALE_EVIDENCE,
    REVALIDATION_REQUIRED,
    WRONG_GENERATION,
    WRONG_EPOCH,
    POLICY_DENIED,
    FAILURE_DOMAIN_CONFLICT,
    NO_ELIGIBLE_CAPACITY
};

// Typed selection outcome for fallback/reject semantics.
enum class SelectionOutcome : std::uint8_t {
    EXPANSION_SELECTED,
    ALTERNATE_EXPANSION_SELECTED,
    LOCAL_FALLBACK_SELECTED,
    STAGING_REQUIRED,
    DEFER,
    REJECT,
    REVALIDATION_REQUIRED
};

// Dynamic evidence status.
enum class EvidenceStatus : std::uint8_t {
    UNKNOWN,
    CURRENT,
    REVALIDATION_REQUIRED,
    STALE
};

// Health of a provider/region as reported by dynamic evidence.
enum class HealthState : std::uint8_t {
    UNKNOWN,
    HEALTHY,
    DEGRADED,
    FAILED
};

// How a consumer reaches a region.
enum class AccessKind : std::uint8_t {
    DIRECT,     // proven direct access
    STAGED,     // staging-required host mediation
    MEDIATED,   // host/device mediation
    UNKNOWN
};

// Locality evidence: generic reachability classification.
enum class Locality : std::uint8_t {
    UNKNOWN,
    LOCAL,      // same node / local to consumer
    REMOTE,     // remote on the fabric
    FABRIC
};

// Addressability model.
enum class Addressability : std::uint8_t {
    UNKNOWN,
    BYTE_ADDRESSABLE,
    BLOCK_ORIENTED
};

// Persistence characteristic of a domain.
enum class Persistence : std::uint8_t {
    UNKNOWN,
    VOLATILE,
    PERSISTENT
};

} // namespace memory_expansion_fabric
