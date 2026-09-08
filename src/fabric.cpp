#include "fabric_impl.hpp"
#include "memory_expansion_fabric/capability.hpp"

#include <chrono>
#include <algorithm>

namespace memory_expansion_fabric {

namespace {
std::uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
std::string errMsg(const char* m) { return std::string(m); }

// Validate a descriptor's capability set for obvious contradictions.
bool capabilityContradiction(const CapabilityModel& c, std::string* why) {
    if (c.get(CapabilityKey::PERSISTENT) == CapabilityStatus::SUPPORTED &&
        c.get(CapabilityKey::VOLATILE) == CapabilityStatus::SUPPORTED) {
        if (why) *why = "PERSISTENT and VOLATILE cannot both be SUPPORTED";
        return true;
    }
    if (c.get(CapabilityKey::BYTE_ADDRESSABLE) == CapabilityStatus::SUPPORTED &&
        c.get(CapabilityKey::BLOCK_ORIENTED) == CapabilityStatus::SUPPORTED) {
        if (why) *why = "BYTE_ADDRESSABLE and BLOCK_ORIENTED cannot both be SUPPORTED";
        return true;
    }
    return false;
}
} // namespace

// ---------------------------------------------------------------------------
// Lifecycle guard tables.
// ---------------------------------------------------------------------------
bool Fabric::Impl::providerLifecycleAllowed(Lifecycle from, Lifecycle to) const {
    if (from == to) return false;
    switch (from) {
        case Lifecycle::DISCOVERED:
            return to == Lifecycle::REGISTERED || to == Lifecycle::RETIRED;
        case Lifecycle::REGISTERED:
            return to == Lifecycle::PROBING || to == Lifecycle::ONLINE ||
                   to == Lifecycle::OFFLINE || to == Lifecycle::RETIRED ||
                   to == Lifecycle::REVALIDATION_REQUIRED;
        case Lifecycle::PROBING:
            return to == Lifecycle::ONLINE || to == Lifecycle::DEGRADED ||
                   to == Lifecycle::OFFLINE || to == Lifecycle::FAILED ||
                   to == Lifecycle::REVALIDATION_REQUIRED || to == Lifecycle::RETIRED;
        case Lifecycle::ONLINE:
            return to == Lifecycle::DEGRADED || to == Lifecycle::DRAINING ||
                   to == Lifecycle::OFFLINE || to == Lifecycle::REVALIDATION_REQUIRED ||
                   to == Lifecycle::RETIRED || to == Lifecycle::FAILED;
        case Lifecycle::DEGRADED:
            return to == Lifecycle::ONLINE || to == Lifecycle::DRAINING ||
                   to == Lifecycle::OFFLINE || to == Lifecycle::REVALIDATION_REQUIRED ||
                   to == Lifecycle::RETIRED || to == Lifecycle::FAILED;
        case Lifecycle::DRAINING:
            return to == Lifecycle::OFFLINE || to == Lifecycle::FAILED ||
                   to == Lifecycle::RETIRED;
        case Lifecycle::OFFLINE:
            return to == Lifecycle::REVALIDATION_REQUIRED || to == Lifecycle::RETIRED ||
                   to == Lifecycle::ONLINE || to == Lifecycle::REGISTERED;
        case Lifecycle::REVALIDATION_REQUIRED:
            return to == Lifecycle::ONLINE || to == Lifecycle::OFFLINE ||
                   to == Lifecycle::RETIRED || to == Lifecycle::PROBING;
        case Lifecycle::FAILED:
            return to == Lifecycle::OFFLINE || to == Lifecycle::RETIRED ||
                   to == Lifecycle::REVALIDATION_REQUIRED || to == Lifecycle::PROBING;
        case Lifecycle::RETIRED:
            return false;
    }
    return false;
}

bool Fabric::Impl::regionLifecycleAllowed(RegionLifecycle from, RegionLifecycle to) const {
    if (from == to) return false;
    switch (from) {
        case RegionLifecycle::DECLARED:
            return to == RegionLifecycle::ONLINE || to == RegionLifecycle::DEGRADED ||
                   to == RegionLifecycle::UNAVAILABLE ||
                   to == RegionLifecycle::REVALIDATION_REQUIRED || to == RegionLifecycle::RETIRED;
        case RegionLifecycle::ONLINE:
            return to == RegionLifecycle::DEGRADED || to == RegionLifecycle::DRAINING ||
                   to == RegionLifecycle::UNAVAILABLE || to == RegionLifecycle::REVALIDATION_REQUIRED ||
                   to == RegionLifecycle::RETIRED;
        case RegionLifecycle::DEGRADED:
            return to == RegionLifecycle::ONLINE || to == RegionLifecycle::DRAINING ||
                   to == RegionLifecycle::UNAVAILABLE || to == RegionLifecycle::REVALIDATION_REQUIRED ||
                   to == RegionLifecycle::RETIRED;
        case RegionLifecycle::DRAINING:
            return to == RegionLifecycle::UNAVAILABLE || to == RegionLifecycle::REVALIDATION_REQUIRED ||
                   to == RegionLifecycle::RETIRED;
        case RegionLifecycle::UNAVAILABLE:
            return to == RegionLifecycle::ONLINE || to == RegionLifecycle::REVALIDATION_REQUIRED ||
                   to == RegionLifecycle::RETIRED;
        case RegionLifecycle::REVALIDATION_REQUIRED:
            return to == RegionLifecycle::ONLINE || to == RegionLifecycle::UNAVAILABLE ||
                   to == RegionLifecycle::DECLARED || to == RegionLifecycle::RETIRED;
        case RegionLifecycle::RETIRED:
            return false;
    }
    return false;
}

bool Fabric::Impl::reservationStateAllowed(ReservationState from, ReservationState to) const {
    if (from == to) return false;
    switch (from) {
        case ReservationState::REQUESTED:
            return to == ReservationState::EVALUATED || to == ReservationState::REJECTED ||
                   to == ReservationState::CANCELLED || to == ReservationState::EXPIRED ||
                   to == ReservationState::REVALIDATION_REQUIRED ||
                   to == ReservationState::RESERVED;
        case ReservationState::EVALUATED:
            return to == ReservationState::RESERVED || to == ReservationState::REJECTED ||
                   to == ReservationState::CANCELLED || to == ReservationState::REVALIDATION_REQUIRED ||
                   to == ReservationState::EXPIRED;
        case ReservationState::RESERVED:
            return to == ReservationState::COMMITTED || to == ReservationState::RELEASED ||
                   to == ReservationState::FENCED || to == ReservationState::REVALIDATION_REQUIRED ||
                   to == ReservationState::EXPIRED || to == ReservationState::CANCELLED;
        case ReservationState::COMMITTED:
            return to == ReservationState::ACTIVE || to == ReservationState::RELEASED ||
                   to == ReservationState::FENCED || to == ReservationState::REVALIDATION_REQUIRED;
        case ReservationState::ACTIVE:
            return to == ReservationState::RELEASED || to == ReservationState::FENCED ||
                   to == ReservationState::REVALIDATION_REQUIRED;
        case ReservationState::RELEASED:
        case ReservationState::REJECTED:
        case ReservationState::EXPIRED:
        case ReservationState::FENCED:
        case ReservationState::CANCELLED:
            return false;
        case ReservationState::REVALIDATION_REQUIRED:
            return to == ReservationState::RESERVED || to == ReservationState::COMMITTED ||
                   to == ReservationState::RELEASED || to == ReservationState::FENCED ||
                   to == ReservationState::CANCELLED;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Authority acceptance. Requires stateMtx held (unique or shared).
// ---------------------------------------------------------------------------
bool Fabric::Impl::authorityAcceptableLocked(const Authority& a) const {
    if (a.epoch != epoch) return false;
    if (a.worker.null()) return true;         // operator/local authority
    auto it = workers.find(a.worker);
    if (it == workers.end()) return false;
    if (it->second.boot != a.boot) return false;
    if (it->second.alive == false || it->second.registeredEpoch != epoch) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Reservation fencing (assume stateMtx held unique).
// ---------------------------------------------------------------------------
bool Fabric::Impl::fence(Fabric::Impl& st, Reservation& r, const char* reason) {
    auto rit = st.regions.find(r.region);
    if (rit != st.regions.end()) {
        RegionRecord& rr = rit->second;
        if (r.state == ReservationState::RESERVED) {
            if (!rr.ledger.fenceReserved(r.bytes)) return false;
            r.state = ReservationState::FENCED;
        } else if (r.state == ReservationState::COMMITTED ||
                   r.state == ReservationState::ACTIVE) {
            if (!rr.ledger.fenceCommitted(r.bytes)) return false;
            r.state = ReservationState::REVALIDATION_REQUIRED;
        } else if (r.state == ReservationState::REQUESTED ||
                   r.state == ReservationState::EVALUATED) {
            r.state = ReservationState::CANCELLED;
        } else {
            return true; // already terminal
        }
        r.note = reason;
        return true;
    }
    // Region gone: cannot adjust the ledger, just mark the fence.
    if (r.state == ReservationState::RESERVED || r.state == ReservationState::COMMITTED ||
        r.state == ReservationState::ACTIVE) {
        r.state = ReservationState::FENCED;
    } else if (r.state == ReservationState::REQUESTED ||
               r.state == ReservationState::EVALUATED) {
        r.state = ReservationState::CANCELLED;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Construction / destruction / move.
// ---------------------------------------------------------------------------
Fabric::Fabric(CoordinatorEpoch epoch) : impl_(std::make_unique<Impl>()) {
    impl_->epoch = epoch;
}

Fabric::~Fabric() = default;
Fabric::Fabric(Fabric&&) noexcept = default;
Fabric& Fabric::operator=(Fabric&&) noexcept = default;

CoordinatorEpoch Fabric::epoch() const {
    std::shared_lock lk(impl_->stateMtx);
    return impl_->epoch;
}

void Fabric::setEpoch(CoordinatorEpoch e) {
    std::unique_lock lk(impl_->stateMtx);
    if (e.value() <= impl_->epoch.value()) return;   // never rewind
    impl_->epoch = e;
    // Fence every worker and every piece of dynamic authority.
    for (auto& [id, wr] : impl_->workers) { (void)id; wr.alive = false; }
    for (auto& [rid, rr] : impl_->regions) {
        (void)rid;
        if (rr.hasEvidence) rr.evidenceStatus = EvidenceStatus::REVALIDATION_REQUIRED;
        if (rr.lifecycle != RegionLifecycle::RETIRED &&
            rr.lifecycle != RegionLifecycle::DRAINING &&
            rr.lifecycle != RegionLifecycle::UNAVAILABLE) {
            rr.lifecycle = RegionLifecycle::REVALIDATION_REQUIRED;
        }
    }
    for (auto& [id, rrec] : impl_->reservations) {
        (void)id;
        Reservation& r = rrec.value;
        if (r.state == ReservationState::RESERVED) {
            auto rit = impl_->regions.find(r.region);
            if (rit != impl_->regions.end()) rit->second.ledger.fenceReserved(r.bytes);
            r.state = ReservationState::REVALIDATION_REQUIRED;
        } else if (r.state == ReservationState::COMMITTED || r.state == ReservationState::ACTIVE) {
            auto rit = impl_->regions.find(r.region);
            if (rit != impl_->regions.end()) rit->second.ledger.fenceCommitted(r.bytes);
            r.state = ReservationState::REVALIDATION_REQUIRED;
        } else if (r.state == ReservationState::REQUESTED || r.state == ReservationState::EVALUATED) {
            r.state = ReservationState::CANCELLED;
        }
    }
}

// ---------------------------------------------------------------------------
// Consumer registry.
// ---------------------------------------------------------------------------
Result<void> Fabric::registerConsumer(const ConsumerId& c, const ConsumerGeneration& g,
                                      const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (c.null() || g.null())
        return Result<void>::fail(ErrorCode::INVALID_ID, "null consumer/generation");
    auto it = impl_->consumers.find(c);
    if (it != impl_->consumers.end()) {
        if (g.value() <= it->second.generation.value())
            return Result<void>::fail(ErrorCode::WRONG_GENERATION, "consumer generation not newer");
    }
    ConsumerRecord rec; rec.generation = g;
    impl_->consumers[c] = rec;
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Registration.
// ---------------------------------------------------------------------------
Result<ProviderId> Fabric::registerProvider(const ProviderDescriptor& d, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<ProviderId>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (d.id.null() || d.generation.null())
        return Result<ProviderId>::fail(ErrorCode::INVALID_ID, "null provider id/generation");
    if (d.totalCapacityBytes == 0)
        return Result<ProviderId>::fail(ErrorCode::INVALID_ARGUMENT, "zero total capacity");
    if (d.alignmentBytes == 0 || d.granularityBytes == 0)
        return Result<ProviderId>::fail(ErrorCode::INVALID_ARGUMENT, "zero alignment/granularity");
    std::string why;
    if (capabilityContradiction(d.capabilities, &why))
        return Result<ProviderId>::fail(ErrorCode::INVALID_ARGUMENT, why);

    auto it = impl_->providers.find(d.id);
    if (it != impl_->providers.end()) {
        const auto& cur = it->second.descriptor;
        if (cur.generation == d.generation)
            return Result<ProviderId>::fail(ErrorCode::ALREADY_EXISTS, "provider already registered");
        if (d.generation.value() < cur.generation.value())
            return Result<ProviderId>::fail(ErrorCode::WRONG_GENERATION, "stale provider generation");
        // Reincarnation: fence all reservations under this provider, mark its
        // regions REVALIDATION_REQUIRED, then replace the record.
        for (auto& [rid, rrec] : impl_->reservations) {
            (void)rid;
            if (rrec.value.provider == d.id)
                Impl::fence(*impl_, rrec.value, "provider reincarnation");
        }
        for (auto& [rid, rr] : impl_->regions) {
            (void)rid;
            if (rr.descriptor.provider == d.id) {
                if (rr.lifecycle != RegionLifecycle::RETIRED)
                    rr.lifecycle = RegionLifecycle::REVALIDATION_REQUIRED;
                rr.hasEvidence = false;
                rr.evidenceStatus = EvidenceStatus::UNKNOWN;
            }
        }
    }
    ProviderRecord rec;
    rec.descriptor = d;
    rec.lifecycle = Lifecycle::REGISTERED;
    rec.registeredAtMs = nowMs();
    impl_->providers[d.id] = std::move(rec);
    return Result<ProviderId>::ok(d.id);
}

Result<ExpansionDomainId> Fabric::registerDomain(const ExpansionDomainDescriptor& d, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<ExpansionDomainId>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (d.id.null() || d.generation.null())
        return Result<ExpansionDomainId>::fail(ErrorCode::INVALID_ID, "null domain id/generation");
    auto it = impl_->domains.find(d.id);
    if (it != impl_->domains.end()) {
        if (it->second.descriptor.generation == d.generation)
            return Result<ExpansionDomainId>::fail(ErrorCode::ALREADY_EXISTS, "domain already registered");
        if (d.generation.value() < it->second.descriptor.generation.value())
            return Result<ExpansionDomainId>::fail(ErrorCode::WRONG_GENERATION, "stale domain generation");
    }
    DomainRecord rec; rec.descriptor = d; rec.lifecycle = Lifecycle::REGISTERED;
    impl_->domains[d.id] = std::move(rec);
    return Result<ExpansionDomainId>::ok(d.id);
}

Result<RegionId> Fabric::registerRegion(const RegionDescriptor& d, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<RegionId>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (d.id.null() || d.generation.null())
        return Result<RegionId>::fail(ErrorCode::INVALID_ID, "null region id/generation");
    if (d.provider.null())
        return Result<RegionId>::fail(ErrorCode::INVALID_ARGUMENT, "region needs canonical backing provider");
    if (d.totalCapacityBytes == 0)
        return Result<RegionId>::fail(ErrorCode::INVALID_ARGUMENT, "zero total capacity");
    if (d.alignmentBytes == 0 || d.granularityBytes == 0)
        return Result<RegionId>::fail(ErrorCode::INVALID_ARGUMENT, "zero alignment/granularity");
    auto pit = impl_->providers.find(d.provider);
    if (pit == impl_->providers.end())
        return Result<RegionId>::fail(ErrorCode::NOT_FOUND, "backing provider not registered");
    std::string why;
    if (capabilityContradiction(d.staticCapabilities, &why))
        return Result<RegionId>::fail(ErrorCode::INVALID_ARGUMENT, why);

    auto it = impl_->regions.find(d.id);
    if (it != impl_->regions.end()) {
        if (it->second.descriptor.generation == d.generation)
            return Result<RegionId>::fail(ErrorCode::ALREADY_EXISTS, "region already registered");
        if (d.generation.value() < it->second.descriptor.generation.value())
            return Result<RegionId>::fail(ErrorCode::WRONG_GENERATION, "stale region generation");
        // Reincarnation: fence old reservations for this region, reset ledger.
        for (auto& [rid, rrec] : impl_->reservations) {
            (void)rid;
            if (rrec.value.region == d.id)
                Impl::fence(*impl_, rrec.value, "region reincarnation");
        }
    }
    RegionRecord rec;
    rec.descriptor = d;
    rec.lifecycle = RegionLifecycle::DECLARED;
    rec.ledger = CapacityLedger(d.totalCapacityBytes);
    impl_->regions[d.id] = std::move(rec);
    return Result<RegionId>::ok(d.id);
}

Result<PoolId> Fabric::registerPool(const PoolDescriptor& d, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<PoolId>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (d.id.null() || d.generation.null())
        return Result<PoolId>::fail(ErrorCode::INVALID_ID, "null pool id/generation");
    auto it = impl_->pools.find(d.id);
    if (it != impl_->pools.end()) {
        if (it->second.descriptor.generation == d.generation)
            return Result<PoolId>::fail(ErrorCode::ALREADY_EXISTS, "pool already registered");
        if (d.generation.value() < it->second.descriptor.generation.value())
            return Result<PoolId>::fail(ErrorCode::WRONG_GENERATION, "stale pool generation");
        for (auto& [rid, rrec] : impl_->reservations) {
            (void)rid;
            if (rrec.value.pool == d.id)
                Impl::fence(*impl_, rrec.value, "pool reincarnation");
        }
    }
    PoolRecord rec; rec.descriptor = d; rec.registeredAtMs = nowMs();
    impl_->pools[d.id] = std::move(rec);
    return Result<PoolId>::ok(d.id);
}

Result<PolicyId> Fabric::registerPolicy(const PolicyDescriptor& d, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<PolicyId>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (d.id.null() || d.generation.null())
        return Result<PolicyId>::fail(ErrorCode::INVALID_ID, "null policy id/generation");
    auto it = impl_->policies.find(d.id);
    if (it != impl_->policies.end()) {
        if (it->second.descriptor.generation == d.generation)
            return Result<PolicyId>::fail(ErrorCode::ALREADY_EXISTS, "policy already registered");
        if (d.generation.value() < it->second.descriptor.generation.value())
            return Result<PolicyId>::fail(ErrorCode::WRONG_GENERATION, "stale policy generation");
        for (auto& [rid, rrec] : impl_->reservations) {
            (void)rid;
            if (rrec.value.policy == d.id)
                Impl::fence(*impl_, rrec.value, "policy reincarnation");
        }
    }
    PolicyRecord rec; rec.descriptor = d;
    impl_->policies[d.id] = std::move(rec);
    return Result<PolicyId>::ok(d.id);
}

Result<void> Fabric::addPoolMember(const PoolId& pool, const PoolMember& m, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto pit = impl_->pools.find(pool);
    if (pit == impl_->pools.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "pool not found");
    auto rit = impl_->regions.find(m.region);
    if (rit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "member region not found");
    if (m.generation.null())
        return Result<void>::fail(ErrorCode::INVALID_ID, "null member generation");
    // Must match current region generation.
    if (rit->second.descriptor.generation != m.generation)
        return Result<void>::fail(ErrorCode::WRONG_GENERATION, "member generation mismatch");
    auto& members = pit->second.descriptor.members;
    for (const auto& mem : members) {
        if (mem.region == m.region) {
            if (mem.generation == m.generation)
                return Result<void>::fail(ErrorCode::ALREADY_EXISTS, "member already present");
            // replace with newer generation
            pit->second.descriptor.members.push_back(m);
            return Result<void>::success();
        }
    }
    members.push_back(m);
    // record membership on region for reverse lookup (pool generation)
    rit->second.poolMemberships.push_back({pool, pit->second.descriptor.generation});
    return Result<void>::success();
}

Result<void> Fabric::removePoolMember(const PoolId& pool, const RegionId& region, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto pit = impl_->pools.find(pool);
    if (pit == impl_->pools.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "pool not found");
    auto& members = pit->second.descriptor.members;
    auto oldSize = members.size();
    members.erase(std::remove_if(members.begin(), members.end(),
                                 [&](const PoolMember& m){ return m.region == region; }),
                  members.end());
    if (members.size() == oldSize)
        return Result<void>::fail(ErrorCode::NOT_FOUND, "member not in pool");
    auto rit = impl_->regions.find(region);
    if (rit != impl_->regions.end()) {
        auto& pm = rit->second.poolMemberships;
        pm.erase(std::remove_if(pm.begin(), pm.end(),
                                [&](const auto& pr){ return pr.first == pool; }), pm.end());
    }
    return Result<void>::success();
}

Result<void> Fabric::replacePoolGeneration(const PoolDescriptor& d, const Authority& a) {
    auto r = registerPool(d, a);
    if (!r.ok()) return Result<void>::fail(r.error());
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Dynamic evidence publication.
// ---------------------------------------------------------------------------
Result<void> Fabric::publishRegionEvidence(const RegionEvidence& e, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (e.header.epoch != impl_->epoch)
        return Result<void>::fail(ErrorCode::WRONG_EPOCH, "evidence epoch mismatch");
    auto rit = impl_->regions.find(e.header.region);
    if (rit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "region not registered");
    RegionRecord& rr = rit->second;
    if (e.header.regionGeneration != rr.descriptor.generation)
        return Result<void>::fail(ErrorCode::WRONG_GENERATION,
                                  "evidence region generation mismatch");
    // Provider must exist and generation must match.
    auto pit = impl_->providers.find(e.header.sourceProvider);
    if (pit == impl_->providers.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "evidence source provider not registered");
    if (e.header.sourceProviderGeneration != pit->second.descriptor.generation)
        return Result<void>::fail(ErrorCode::WRONG_GENERATION, "evidence provider generation mismatch");
    // If evidence is authored by a worker, it must match the authority worker.
    if (!a.worker.null() && (e.header.worker != a.worker || e.header.boot != a.boot))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "evidence worker mismatch");
    // Online capacity must be <= total and must not contradict obligations.
    if (e.onlineCapacityBytes > rr.descriptor.totalCapacityBytes)
        return Result<void>::fail(ErrorCode::INVALID_ARGUMENT, "online capacity exceeds region total");
    // Only accept evidence into DECLARED/ONLINE/DEGRADED/UNAVAILABLE/REVALIDATION_REQUIRED,
    // never into DRAINING/RETIRED (those are terminal-ish for this publisher).
    if (rr.lifecycle == RegionLifecycle::DRAINING || rr.lifecycle == RegionLifecycle::RETIRED)
        return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION,
                                  "cannot publish evidence into DRAINING/RETIRED region");
    // Apply online capacity; reject if it would underflow obligations.
    if (!rr.ledger.setOnline(e.onlineCapacityBytes))
        return Result<void>::fail(ErrorCode::INVALID_ARGUMENT,
                                  "evidence online capacity contradicts obligations");
    rr.latest = e;
    rr.hasEvidence = true;
    rr.evidenceStatus = EvidenceStatus::CURRENT;
    rr.evidenceAtMs = nowMs();
    rr.health = e.health;
    rr.reachable = e.reachable;
    rr.latencyNs = e.latencyNs;
    rr.bandwidthBytesPerSec = e.bandwidthBytesPerSec;

    // Lifecycle update based on health. Reachability is a separate evidence
    // flag (an ONLINE region may be unreachable). Health drives the lifecycle.
    RegionLifecycle target = rr.lifecycle;
    if (e.health == HealthState::FAILED)
        target = RegionLifecycle::REVALIDATION_REQUIRED;
    else if (e.health == HealthState::DEGRADED)
        target = RegionLifecycle::DEGRADED;
    else
        target = RegionLifecycle::ONLINE;
    if (target != rr.lifecycle) {
        if (!impl_->regionLifecycleAllowed(rr.lifecycle, target))
            return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION,
                                      "evidence lifecycle transition not allowed");
        rr.lifecycle = target;
    }

    // Reflect on provider lifecycle.
    auto pit2 = impl_->providers.find(rr.descriptor.provider);
    if (pit2 != impl_->providers.end()) {
        Lifecycle pt = pit2->second.lifecycle;
        Lifecycle ptTarget = pt;
        if (rr.lifecycle == RegionLifecycle::ONLINE && (pt == Lifecycle::REGISTERED ||
            pt == Lifecycle::PROBING || pt == Lifecycle::OFFLINE ||
            pt == Lifecycle::REVALIDATION_REQUIRED)) ptTarget = Lifecycle::ONLINE;
        if (ptTarget != pt && impl_->providerLifecycleAllowed(pt, ptTarget))
            pit2->second.lifecycle = ptTarget;
    }
    return Result<void>::success();
}

Result<void> Fabric::publishAttachment(const AttachmentEvidence& e, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (e.epoch != impl_->epoch)
        return Result<void>::fail(ErrorCode::WRONG_EPOCH, "attachment epoch mismatch");
    if (e.id.null() || e.generation.null())
        return Result<void>::fail(ErrorCode::INVALID_ID, "null attachment id/generation");
    auto rit = impl_->regions.find(e.region);
    if (rit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "region not registered");
    RegionRecord& rr = rit->second;
    if (e.regionGeneration != rr.descriptor.generation)
        return Result<void>::fail(ErrorCode::WRONG_GENERATION, "attachment region generation mismatch");
    // find existing attachment, replace, else append
    bool found = false;
    for (auto& att : rr.attachments) {
        if (att.id == e.id) { att = e; found = true; break; }
    }
    if (!found) rr.attachments.push_back(e);
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Lifecycle transitions.
// ---------------------------------------------------------------------------
Result<void> Fabric::beginDrain(const RegionId& region, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto rit = impl_->regions.find(region);
    if (rit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "region not found");
    RegionRecord& rr = rit->second;
    if (!impl_->regionLifecycleAllowed(rr.lifecycle, RegionLifecycle::DRAINING))
        return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION, "cannot begin drain");
    // Move all free capacity into draining (no new reservations).
    if (!rr.ledger.beginDrain(rr.ledger.free()))
        return Result<void>::fail(ErrorCode::INVALID_ARGUMENT, "drain move failed");
    // Reject outstanding RESERVED reservations (they cannot be used during drain).
    for (auto& [rid, rrec] : impl_->reservations) {
        (void)rid;
        if (rrec.value.region == region &&
            rrec.value.state == ReservationState::RESERVED) {
            rrec.value.state = ReservationState::CANCELLED;
        }
    }
    rr.lifecycle = RegionLifecycle::DRAINING;
    auto pit = impl_->providers.find(rr.descriptor.provider);
    if (pit != impl_->providers.end() && pit->second.lifecycle == Lifecycle::ONLINE)
        pit->second.lifecycle = Lifecycle::DRAINING;
    return Result<void>::success();
}

Result<void> Fabric::completeDrain(const RegionId& region, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto rit = impl_->regions.find(region);
    if (rit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "region not found");
    RegionRecord& rr = rit->second;
    if (rr.lifecycle != RegionLifecycle::DRAINING)
        return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION, "region not draining");
    // Committed authority must have been handled; move draining -> unavailable.
    if (!rr.ledger.endDrain(rr.ledger.draining()))
        return Result<void>::fail(ErrorCode::INVALID_ARGUMENT, "complete drain failed");
    rr.lifecycle = RegionLifecycle::UNAVAILABLE;
    auto pit = impl_->providers.find(rr.descriptor.provider);
    if (pit != impl_->providers.end() && pit->second.lifecycle == Lifecycle::DRAINING)
        pit->second.lifecycle = Lifecycle::OFFLINE;
    return Result<void>::success();
}

Result<void> Fabric::cancelDrain(const RegionId& region, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto rit = impl_->regions.find(region);
    if (rit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "region not found");
    RegionRecord& rr = rit->second;
    if (rr.lifecycle != RegionLifecycle::DRAINING)
        return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION, "region not draining");
    if (!rr.ledger.cancelDrain(rr.ledger.draining()))
        return Result<void>::fail(ErrorCode::INVALID_ARGUMENT, "cancel drain failed");
    rr.lifecycle = RegionLifecycle::ONLINE;
    return Result<void>::success();
}

Result<void> Fabric::markUnavailable(const RegionId& region, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto rit = impl_->regions.find(region);
    if (rit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "region not found");
    RegionRecord& rr = rit->second;
    if (!impl_->regionLifecycleAllowed(rr.lifecycle, RegionLifecycle::UNAVAILABLE))
        return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION, "cannot mark unavailable");
    // Move free + fence all reserved/committed into unavailable.
    if (!rr.ledger.markUnavailable(rr.ledger.free()))
        return Result<void>::fail(ErrorCode::INVALID_ARGUMENT, "unavailable move failed");
    for (auto& [rid, rrec] : impl_->reservations) {
        (void)rid;
        if (rrec.value.region == region)
            Impl::fence(*impl_, rrec.value, "capacity unavailable");
    }
    rr.lifecycle = RegionLifecycle::UNAVAILABLE;
    return Result<void>::success();
}

Result<void> Fabric::recoverRegion(const RegionId& region, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto rit = impl_->regions.find(region);
    if (rit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "region not found");
    RegionRecord& rr = rit->second;
    if (rr.lifecycle != RegionLifecycle::UNAVAILABLE &&
        rr.lifecycle != RegionLifecycle::REVALIDATION_REQUIRED &&
        rr.lifecycle != RegionLifecycle::DEGRADED)
        return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION, "region not recoverable");
    // Restore unavailable capacity to free; require fresh evidence to go online.
    rr.ledger.restore(rr.ledger.unavailable());
    rr.lifecycle = RegionLifecycle::DECLARED;
    rr.evidenceStatus = EvidenceStatus::REVALIDATION_REQUIRED;
    return Result<void>::success();
}

Result<void> Fabric::retireProvider(const ProviderId& provider, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto pit = impl_->providers.find(provider);
    if (pit == impl_->providers.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "provider not found");
    if (!impl_->providerLifecycleAllowed(pit->second.lifecycle, Lifecycle::RETIRED))
        return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION, "cannot retire provider");
    for (auto& [rid, rrec] : impl_->reservations) {
        (void)rid;
        if (rrec.value.provider == provider)
            Impl::fence(*impl_, rrec.value, "provider retired");
    }
    for (auto& [rid, rr] : impl_->regions) {
        if (rr.descriptor.provider == provider && rr.lifecycle != RegionLifecycle::RETIRED)
            rr.lifecycle = RegionLifecycle::RETIRED;
    }
    pit->second.lifecycle = Lifecycle::RETIRED;
    return Result<void>::success();
}

Result<void> Fabric::retireRegion(const RegionId& region, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto rit = impl_->regions.find(region);
    if (rit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "region not found");
    if (!impl_->regionLifecycleAllowed(rit->second.lifecycle, RegionLifecycle::RETIRED))
        return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION, "cannot retire region");
    for (auto& [rid, rrec] : impl_->reservations) {
        (void)rid;
        if (rrec.value.region == region)
            Impl::fence(*impl_, rrec.value, "region retired");
    }
    rit->second.lifecycle = RegionLifecycle::RETIRED;
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Worker registry.
// ---------------------------------------------------------------------------
Result<void> Fabric::registerWorker(const WorkerId& w, const WorkerBootId& boot,
                                    const CoordinatorEpoch& epoch, std::uint64_t pid,
                                    const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (w.null() || boot.null())
        return Result<void>::fail(ErrorCode::INVALID_ID, "null worker id/boot");
    if (a.epoch != impl_->epoch)
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "worker registers under stale epoch");
    if (epoch != impl_->epoch)
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "worker epoch mismatch");
    if (!a.worker.null() && (a.worker != w || a.boot != boot))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "worker authority mismatch");
    auto it = impl_->workers.find(w);
    if (it != impl_->workers.end()) {
        // A boot id is bound to one incarnation. Whether alive or dead, a
        // previously seen boot must never be re-admitted as fresh authority.
        if (it->second.boot == boot)
            return Result<void>::fail(ErrorCode::ALREADY_EXISTS, "worker boot already used");
    }
    WorkerRecord rec;
    rec.boot = boot;
    rec.registeredEpoch = epoch;
    rec.pid = pid;
    rec.alive = true;
    rec.holdsHandle = false;
    rec.lastSeenMs = nowMs();
    impl_->workers[w] = std::move(rec);
    return Result<void>::success();
}

Result<void> Fabric::markWorkerDead(const WorkerId& w, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto it = impl_->workers.find(w);
    if (it == impl_->workers.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "worker not registered");
    WorkerBootId boot = it->second.boot;
    it->second.alive = false;
    it->second.lastSeenMs = nowMs();
    // Fence dynamic evidence owned by this worker, and every reservation
    // whose backing-region authority came from this worker (worker-bound
    // reservations are fenced too).
    std::vector<RegionId> workerRegions;
    for (auto& [rid, rr] : impl_->regions) {
        if (rr.hasEvidence && rr.latest.header.worker == w &&
            rr.latest.header.boot == boot) {
            rr.evidenceStatus = EvidenceStatus::REVALIDATION_REQUIRED;
            if (rr.lifecycle != RegionLifecycle::RETIRED &&
                rr.lifecycle != RegionLifecycle::DRAINING &&
                rr.lifecycle != RegionLifecycle::UNAVAILABLE)
                rr.lifecycle = RegionLifecycle::REVALIDATION_REQUIRED;
            workerRegions.push_back(rid);
        }
    }
    for (auto& [rid, rrec] : impl_->reservations) {
        (void)rid;
        bool byWorker = rrec.value.worker == w;
        bool byRegion = false;
        for (auto r : workerRegions)
            if (rrec.value.region == r) { byRegion = true; break; }
        if (byWorker || byRegion)
            Impl::fence(*impl_, rrec.value, "worker died");
    }
    return Result<void>::success();
}

bool Fabric::workerAlive(const WorkerId& w, const WorkerBootId& boot) const {
    std::shared_lock lk(impl_->stateMtx);
    auto it = impl_->workers.find(w);
    if (it == impl_->workers.end()) return false;
    return it->second.alive && it->second.boot == boot;
}

void Fabric::noteWorkerHandle(const WorkerId& w, bool holdsHandle) {
    std::unique_lock lk(impl_->stateMtx);
    auto it = impl_->workers.find(w);
    if (it != impl_->workers.end()) it->second.holdsHandle = holdsHandle;
}

std::uint64_t Fabric::regionFree(const RegionId& region) const {
    std::shared_lock lk(impl_->stateMtx);
    auto it = impl_->regions.find(region);
    if (it == impl_->regions.end()) return 0;
    return it->second.ledger.free();
}

} // namespace memory_expansion_fabric
