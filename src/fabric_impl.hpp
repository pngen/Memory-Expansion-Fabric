#pragma once
// Internal implementation structures for Fabric. NOT installed.
#include "memory_expansion_fabric/fabric.hpp"
#include "memory_expansion_fabric/capacity.hpp"

#include <unordered_map>
#include <map>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <cstdint>
#include <string>
#include <utility>

namespace memory_expansion_fabric {

struct ProviderRecord {
    ProviderDescriptor descriptor;
    Lifecycle lifecycle = Lifecycle::DISCOVERED;
    std::uint64_t registeredAtMs = 0;
};

struct DomainRecord {
    ExpansionDomainDescriptor descriptor;
    Lifecycle lifecycle = Lifecycle::DISCOVERED;
};

struct RegionRecord {
    RegionDescriptor descriptor;
    RegionLifecycle lifecycle = RegionLifecycle::DECLARED;
    CapacityLedger ledger{0};
    RegionEvidence latest;
    bool hasEvidence = false;
    EvidenceStatus evidenceStatus = EvidenceStatus::UNKNOWN;
    HealthState health = HealthState::UNKNOWN;
    bool reachable = false;
    std::uint64_t latencyNs = 0;
    std::uint64_t bandwidthBytesPerSec = 0;
    std::vector<AttachmentEvidence> attachments;
    std::vector<std::pair<PoolId, PoolGeneration>> poolMemberships;
    std::uint64_t evidenceAtMs = 0;
};

struct PoolRecord {
    PoolDescriptor descriptor;
    std::uint64_t registeredAtMs = 0;
};

struct PolicyRecord {
    PolicyDescriptor descriptor;
};

struct ConsumerRecord {
    ConsumerGeneration generation;
};

struct ReservationRecord {
    Reservation value;
};

struct WorkerRecord {
    WorkerBootId boot;
    CoordinatorEpoch registeredEpoch;
    std::uint64_t pid = 0;
    bool alive = false;
    bool holdsHandle = false;
    std::uint64_t lastSeenMs = 0;
};

struct Fabric::Impl {
    CoordinatorEpoch epoch{1};
    std::unordered_map<ProviderId, ProviderRecord> providers;
    std::unordered_map<ExpansionDomainId, DomainRecord> domains;
    std::unordered_map<RegionId, RegionRecord> regions;
    std::unordered_map<PoolId, PoolRecord> pools;
    std::unordered_map<PolicyId, PolicyRecord> policies;
    std::unordered_map<ConsumerId, ConsumerRecord> consumers;
    std::unordered_map<ReservationId, ReservationRecord> reservations;
    std::unordered_map<WorkerId, WorkerRecord> workers;

    std::shared_mutex stateMtx;           // guards ALL fabric state (incl. workers)

    // id allocators for runtime-generated ids. These live outside the fabric
    // state lock so selection (a read op) may safely mint selection ids.
    IdAllocator selectionIdAlloc;
    IdAllocator selectionGenAlloc;
    IdAllocator reservationIdAlloc;
    IdAllocator reservationGenAlloc;
    IdAllocator evidenceIdAlloc;
    IdAllocator evidenceGenAlloc;

    std::uint64_t nowMs = 0;              // monotonic clock used for evidence

    // --- authority / lifecycle guards --------------------------------------
    // Requires workersMtx held or callers pass a consistent snapshot.
    bool authorityAcceptableLocked(const Authority& a) const;

    bool providerLifecycleAllowed(Lifecycle from, Lifecycle to) const;
    bool regionLifecycleAllowed(RegionLifecycle from, RegionLifecycle to) const;
    bool reservationStateAllowed(ReservationState from, ReservationState to) const;

    // Fence (or cancel) a single reservation and adjust its region ledger.
    // Returns true if the reservation was newly fenced/cancelled, false if the
    // ledger could not be adjusted (should not happen given invariants).
    static bool fence(Impl& st, Reservation& r, const char* reason);

    // Shared selection core: eligibility + deterministic ranking. No locking
    // (caller holds stateMtx, shared or unique). Mints a fresh selection id.
    SelectionResult evaluateLocked(const ConsumerRequirements& req);

    // --- helpers (caller must hold appropriate lock) -------------------------
    // Returns ELIGIBLE when the region passes every hard constraint, or a
    // typed hard-rejection reason otherwise.
    EligibilityReason evaluateRegionEligibility(const ConsumerRequirements& req,
                                                const RegionRecord& rr) const;
};

} // namespace memory_expansion_fabric
