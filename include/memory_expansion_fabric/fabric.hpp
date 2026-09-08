#pragma once
// Memory Expansion Fabric - public runtime entry point.
//
// This is the generic expanded-memory control plane. It owns domain identity,
// provider/region/pool structure, capability & health state, capacity
// accounting, eligibility, deterministic ranking, reservation/commit/drain
// lifecycle, generation-bound authority, persistence and inspection.
//
// It does NOT own malloc/free, GPU alloca, Transfer Fabric movement, cache
// semantics, CXL protocol, RDMA, or storage paging (see README).
#include "memory_expansion_fabric/id.hpp"
#include "memory_expansion_fabric/enums.hpp"
#include "memory_expansion_fabric/authority.hpp"
#include "memory_expansion_fabric/result.hpp"
#include "memory_expansion_fabric/types.hpp"

#include <memory>
#include <string>

namespace memory_expansion_fabric {

class Fabric {
public:
    // A Fabric lives in a coordinator epoch. The epoch is part of the
    // authority model; stale-epoch traffic is rejected.
    explicit Fabric(CoordinatorEpoch epoch = CoordinatorEpoch(1));
    ~Fabric();

    Fabric(const Fabric&) = delete;
    Fabric& operator=(const Fabric&) = delete;
    Fabric(Fabric&&) noexcept;
    Fabric& operator=(Fabric&&) noexcept;

    // --- identity / epoch ---------------------------------------------------
    CoordinatorEpoch epoch() const;
    void setEpoch(CoordinatorEpoch e);   // advance epoch (fences prior workers)

    // --- registration (durable structure) -----------------------------------
    Result<ProviderId> registerProvider(const ProviderDescriptor& d, const Authority& a);
    Result<ExpansionDomainId> registerDomain(const ExpansionDomainDescriptor& d, const Authority& a);
    Result<RegionId> registerRegion(const RegionDescriptor& d, const Authority& a);
    Result<PoolId> registerPool(const PoolDescriptor& d, const Authority& a);
    Result<PolicyId> registerPolicy(const PolicyDescriptor& d, const Authority& a);
    Result<void> addPoolMember(const PoolId& pool, const PoolMember& m, const Authority& a);
    Result<void> removePoolMember(const PoolId& pool, const RegionId& region, const Authority& a);
    Result<void> replacePoolGeneration(const PoolDescriptor& d, const Authority& a);

    // --- dynamic evidence publication (authority-fenced) ----------------------
    Result<void> publishRegionEvidence(const RegionEvidence& e, const Authority& a);
    Result<void> publishAttachment(const AttachmentEvidence& e, const Authority& a);

    // --- lifecycle transitions -------------------------------------------------
    Result<void> beginDrain(const RegionId& region, const Authority& a);
    Result<void> completeDrain(const RegionId& region, const Authority& a);
    Result<void> cancelDrain(const RegionId& region, const Authority& a);
    Result<void> markUnavailable(const RegionId& region, const Authority& a);
    Result<void> recoverRegion(const RegionId& region, const Authority& a);
    Result<void> retireProvider(const ProviderId& provider, const Authority& a);
    Result<void> retireRegion(const RegionId& region, const Authority& a);

    // --- consumer registry -------------------------------------------------------
    Result<void> registerConsumer(const ConsumerId& c, const ConsumerGeneration& g,
                                  const Authority& a);

    // --- worker registry (multiprocess authority) -------------------------------
    Result<void> registerWorker(const WorkerId& w, const WorkerBootId& boot,
                                const CoordinatorEpoch& epoch, std::uint64_t pid,
                                const Authority& a);
    Result<void> markWorkerDead(const WorkerId& w, const Authority& a);
    bool workerAlive(const WorkerId& w, const WorkerBootId& boot) const;
    // Coordinator-side metadata: records whether the coordinator holds a real
    // OS process handle for the worker (used by the worker-death proof).
    void noteWorkerHandle(const WorkerId& w, bool holdsHandle);

    // --- eligibility / deterministic ranking -------------------------------------
    Result<SelectionResult> select(const ConsumerRequirements& req, const Authority& a);

    // --- reservation / commit / release lifecycle ---------------------------------
    Result<ReservationId> requestReservation(const ConsumerRequirements& req, const Authority& a);
    Result<Reservation> commitReservation(const ReservationId& id, const Authority& a);
    Result<void> releaseReservation(const ReservationId& id, const Authority& a);

    // --- inspection ----------------------------------------------------------------
    FabricSnapshot snapshot() const;
    Result<std::string> audit() const;

    // --- persistence ----------------------------------------------------------------
    // Save is atomic and integrity-checked; dynamic recovered evidence never
    // silently becomes current.
    Result<void> save(const std::string& path) const;
    // Load reconstructs a coordinator at the *next* epoch and marks recovered
    // dynamic evidence REVALIDATION_REQUIRED.
    static Result<Fabric> load(const std::string& path);

    // --- lookup helpers (read-only) --------------------------------------------------
    std::uint64_t regionFree(const RegionId& region) const;

public:
    struct Impl;  // opaque; defined internally, never used by consumers
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace memory_expansion_fabric
