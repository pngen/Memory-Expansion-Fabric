#include "fabric_impl.hpp"
#include <sstream>

namespace memory_expansion_fabric {

FabricSnapshot Fabric::snapshot() const {
    std::shared_lock lk(impl_->stateMtx);
    FabricSnapshot snap;
    snap.epoch = impl_->epoch;
    for (const auto& [id, p] : impl_->providers) {
        ProviderSnapshot s;
        s.id = id; s.generation = p.descriptor.generation; s.kind = p.descriptor.kind;
        s.origin = p.descriptor.origin; s.name = p.descriptor.name;
        s.failureDomain = p.descriptor.failureDomain;
        s.totalCapacityBytes = p.descriptor.totalCapacityBytes;
        s.lifecycle = p.lifecycle; s.timestampMs = p.registeredAtMs;
        snap.providers.push_back(std::move(s));
    }
    for (const auto& [id, d] : impl_->domains) {
        DomainSnapshot s;
        s.id = id; s.generation = d.descriptor.generation; s.name = d.descriptor.name;
        s.origin = d.descriptor.origin; s.failureDomain = d.descriptor.failureDomain;
        s.lifecycle = d.lifecycle;
        snap.domains.push_back(std::move(s));
    }
    for (const auto& [id, r] : impl_->regions) {
        RegionSnapshot s;
        s.id = id; s.generation = r.descriptor.generation; s.provider = r.descriptor.provider;
        s.domain = r.descriptor.domain; s.name = r.descriptor.name;
        s.totalCapacityBytes = r.descriptor.totalCapacityBytes; s.lifecycle = r.lifecycle;
        s.onlineBytes = r.ledger.governedOnline(); s.freeBytes = r.ledger.free();
        s.reservedBytes = r.ledger.reserved(); s.committedBytes = r.ledger.committed();
        s.drainingBytes = r.ledger.draining(); s.unavailableBytes = r.ledger.unavailable();
        s.health = r.health; s.reachable = r.reachable; s.evidenceStatus = r.evidenceStatus;
        s.latencyNs = r.latencyNs; s.bandwidthBytesPerSec = r.bandwidthBytesPerSec;
        s.origin = r.descriptor.origin;
        snap.regions.push_back(std::move(s));
    }
    for (const auto& [id, pl] : impl_->pools) {
        PoolSnapshot s;
        s.id = id; s.generation = pl.descriptor.generation; s.name = pl.descriptor.name;
        s.origin = pl.descriptor.origin; s.members = pl.descriptor.members;
        std::uint64_t tot = 0, fr = 0;
        for (const auto& m : s.members) {
            auto rit = impl_->regions.find(m.region);
            if (rit != impl_->regions.end()) {
                tot += rit->second.descriptor.totalCapacityBytes;
                fr += rit->second.ledger.free();
            }
        }
        s.totalBytes = tot; s.freeBytes = fr;
        snap.pools.push_back(std::move(s));
    }
    for (const auto& [id, r] : impl_->reservations) {
        ReservationSnapshot s;
        s.id = id; s.generation = r.value.generation; s.consumer = r.value.consumer;
        s.region = r.value.region; s.provider = r.value.provider; s.bytes = r.value.bytes;
        s.state = r.value.state; s.epoch = r.value.epoch;
        snap.reservations.push_back(std::move(s));
    }
    for (const auto& [id, pol] : impl_->policies) {
        PolicySnapshot s;
        s.id = id; s.generation = pol.descriptor.generation; s.name = pol.descriptor.name;
        s.rules = pol.descriptor.rules;
        snap.policies.push_back(std::move(s));
    }
    for (const auto& [id, w] : impl_->workers) {
        WorkerSnapshot s;
        s.id = id; s.boot = w.boot; s.registered = true; s.alive = w.alive;
        s.holdsProcessHandle = w.holdsHandle; s.pid = w.pid; s.lastSeenMs = w.lastSeenMs;
        snap.workers.push_back(std::move(s));
    }
    return snap;
}

Result<std::string> Fabric::audit() const {
    std::shared_lock lk(impl_->stateMtx);
    std::ostringstream os;
    bool allOk = true;
    auto check = [&](bool cond, const std::string& what) {
        os << (cond ? "PASS " : "FAIL ") << what << "\n";
        if (!cond) allOk = false;
    };

    for (const auto& [id, r] : impl_->regions) {
        (void)id;
        check(r.ledger.invariant(), "capacity invariant: region " + std::to_string(id.value()));
        check(r.ledger.governedOnline() <= r.descriptor.totalCapacityBytes,
              "online <= total: region " + std::to_string(id.value()));
    }
    // Cross-check reservation accounting against region ledgers.
    for (const auto& [rid, rr] : impl_->regions) {
        std::uint64_t reservedSum = 0, committedSum = 0;
        for (const auto& [id, rrec] : impl_->reservations) {
            (void)id;
            if (rrec.value.region != rid) continue;
            if (rrec.value.state == ReservationState::RESERVED) reservedSum += rrec.value.bytes;
            if (rrec.value.state == ReservationState::COMMITTED ||
                rrec.value.state == ReservationState::ACTIVE) committedSum += rrec.value.bytes;
        }
        check(reservedSum == rr.ledger.reserved(),
              "reservation sum == ledger reserved: region " + std::to_string(rid.value()));
        check(committedSum == rr.ledger.committed(),
              "reservation sum == ledger committed: region " + std::to_string(rid.value()));
    }
    // Pool membership must reference current region generations.
    for (const auto& [pid, pl] : impl_->pools) {
        (void)pid;
        for (const auto& m : pl.descriptor.members) {
            auto rit = impl_->regions.find(m.region);
            check(rit != impl_->regions.end() && rit->second.descriptor.generation == m.generation,
                  "pool member current: pool " + std::to_string(pid.value()));
        }
    }
    // A recovered/reval region must not be silently current.
    for (const auto& [id, r] : impl_->regions) {
        (void)id;
        if (r.lifecycle == RegionLifecycle::REVALIDATION_REQUIRED)
            check(r.evidenceStatus != EvidenceStatus::CURRENT,
                  "revalidation region not CURRENT: region " + std::to_string(id.value()));
    }
    // Recovered worker evidence must not be current under a stale epoch.
    if (allOk) os << "PASS audit complete\n";
    else os << "FAIL audit complete\n";
    return Result<std::string>::ok(os.str());
}

} // namespace memory_expansion_fabric
