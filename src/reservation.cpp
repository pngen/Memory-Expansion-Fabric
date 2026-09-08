#include "fabric_impl.hpp"
#include <chrono>

namespace memory_expansion_fabric {

namespace {
std::uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
}

Result<ReservationId> Fabric::requestReservation(const ConsumerRequirements& req, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<ReservationId>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (req.requiredBytes == 0)
        return Result<ReservationId>::fail(ErrorCode::INVALID_ARGUMENT, "reservation requires bytes>0");
    if (req.consumer.null())
        return Result<ReservationId>::fail(ErrorCode::INVALID_ID, "reservation needs a consumer");
    if (req.consumerGeneration.null())
        return Result<ReservationId>::fail(ErrorCode::INVALID_ID, "reservation needs a consumer generation");

    SelectionResult sel = impl_->evaluateLocked(req);
    if (sel.ranked.empty()) {
        // No qualifying expansion capacity.
        return Result<ReservationId>::fail(ErrorCode::CAPACITY_INSUFFICIENT,
            "no eligible expansion capacity");
    }
    const RankedCandidate& top = sel.ranked[0];
    auto rit = impl_->regions.find(top.region);
    if (rit == impl_->regions.end())
        return Result<ReservationId>::fail(ErrorCode::NOT_FOUND, "selected region missing");
    RegionRecord& rr = rit->second;
    if (!rr.ledger.reserve(req.requiredBytes))
        return Result<ReservationId>::fail(ErrorCode::CAPACITY_INSUFFICIENT, "reserve failed");

    Reservation res;
    res.id = impl_->reservationIdAlloc.next<ReservationId>();
    res.generation = impl_->reservationGenAlloc.next<ReservationGeneration>();
    res.consumer = req.consumer;
    res.consumerGeneration = req.consumerGeneration;
    res.provider = rr.descriptor.provider;
    res.region = top.region;
    res.regionGeneration = rr.descriptor.generation;
    res.pool = req.preferredPool;
    res.selection = sel.id;
    res.selectionGeneration = sel.generation;
    res.evidenceGeneration = rr.hasEvidence ? rr.latest.header.generation : EvidenceGeneration();
    res.worker = a.worker;
    res.boot = a.boot;
    res.epoch = a.epoch;
    res.bytes = req.requiredBytes;
    res.state = ReservationState::RESERVED;

    // Bind provider generation.
    auto pit = impl_->providers.find(rr.descriptor.provider);
    if (pit != impl_->providers.end())
        res.providerGeneration = pit->second.descriptor.generation;
    // Bind policy generation.
    if (!req.policy.null()) {
        res.policy = req.policy;
        auto polit = impl_->policies.find(req.policy);
        if (polit != impl_->policies.end())
            res.policyGeneration = polit->second.descriptor.generation;
    }
    // Bind pool generation.
    if (!req.preferredPool.null()) {
        auto poolit = impl_->pools.find(req.preferredPool);
        if (poolit != impl_->pools.end())
            res.poolGeneration = poolit->second.descriptor.generation;
    }
    res.createdAtMs = nowMs();

    impl_->reservations[res.id] = ReservationRecord{res};
    return Result<ReservationId>::ok(res.id);
}

Result<Reservation> Fabric::commitReservation(const ReservationId& id, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<Reservation>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto rit = impl_->reservations.find(id);
    if (rit == impl_->reservations.end())
        return Result<Reservation>::fail(ErrorCode::NOT_FOUND, "reservation not found");
    Reservation& res = rit->second.value;

    if (res.state != ReservationState::RESERVED)
        return Result<Reservation>::fail(ErrorCode::INVALID_STATE_TRANSITION,
                                         "only a RESERVED reservation can be committed");
    if (!impl_->reservationStateAllowed(ReservationState::RESERVED, ReservationState::COMMITTED))
        return Result<Reservation>::fail(ErrorCode::INVALID_STATE_TRANSITION, "illegal commit");

    // Revalidate every generation binding against current state.
    auto rgit = impl_->regions.find(res.region);
    if (rgit == impl_->regions.end())
        return Result<Reservation>::fail(ErrorCode::WRONG_GENERATION, "region gone");
    RegionRecord& rr = rgit->second;
    if (rr.descriptor.generation != res.regionGeneration)
        return Result<Reservation>::fail(ErrorCode::WRONG_GENERATION, "region generation changed");
    auto pit = impl_->providers.find(res.provider);
    if (pit == impl_->providers.end())
        return Result<Reservation>::fail(ErrorCode::WRONG_GENERATION, "provider gone");
    if (pit->second.descriptor.generation != res.providerGeneration)
        return Result<Reservation>::fail(ErrorCode::WRONG_GENERATION, "provider generation changed");
    if (!res.policy.null()) {
        auto polit = impl_->policies.find(res.policy);
        if (polit == impl_->policies.end())
            return Result<Reservation>::fail(ErrorCode::WRONG_GENERATION, "policy gone");
        if (polit->second.descriptor.generation != res.policyGeneration)
            return Result<Reservation>::fail(ErrorCode::WRONG_GENERATION, "policy generation changed");
    }
    if (!res.pool.null()) {
        auto poolit = impl_->pools.find(res.pool);
        if (poolit == impl_->pools.end())
            return Result<Reservation>::fail(ErrorCode::WRONG_GENERATION, "pool gone");
        if (poolit->second.descriptor.generation != res.poolGeneration)
            return Result<Reservation>::fail(ErrorCode::WRONG_GENERATION, "pool generation changed");
    }
    if (res.epoch != impl_->epoch)
        return Result<Reservation>::fail(ErrorCode::WRONG_EPOCH, "reservation epoch stale");
    if (!res.worker.null()) {
        auto wit = impl_->workers.find(res.worker);
        if (wit == impl_->workers.end() || !wit->second.alive ||
            wit->second.boot != res.boot)
            return Result<Reservation>::fail(ErrorCode::STALE_AUTHORITY,
                                             "worker authority stale / dead");
        if (a.worker != res.worker || a.boot != res.boot)
            return Result<Reservation>::fail(ErrorCode::STALE_AUTHORITY, "commit worker mismatch");
    } else {
        if (!a.worker.null())
            return Result<Reservation>::fail(ErrorCode::STALE_AUTHORITY,
                                             "operator reservation cannot be committed by worker");
    }
    if (!res.consumerGeneration.null()) {
        auto cit = impl_->consumers.find(res.consumer);
        if (cit != impl_->consumers.end() &&
            cit->second.generation != res.consumerGeneration)
            return Result<Reservation>::fail(ErrorCode::WRONG_GENERATION, "consumer generation changed");
    }
    if (rr.evidenceStatus != EvidenceStatus::CURRENT ||
        rr.hasEvidence && rr.latest.header.generation != res.evidenceGeneration)
        return Result<Reservation>::fail(ErrorCode::STALE_EVIDENCE, "evidence no longer current");
    if (rr.lifecycle == RegionLifecycle::DRAINING ||
        rr.lifecycle == RegionLifecycle::UNAVAILABLE ||
        rr.lifecycle == RegionLifecycle::REVALIDATION_REQUIRED ||
        rr.lifecycle == RegionLifecycle::RETIRED)
        return Result<Reservation>::fail(ErrorCode::REVALIDATION_REQUIRED,
                                         "region no longer committable");
    if (rr.ledger.reserved() < res.bytes)
        return Result<Reservation>::fail(ErrorCode::CAPACITY_INSUFFICIENT,
                                         "reserved capacity was revoked");

    if (!rr.ledger.commit(res.bytes))
        return Result<Reservation>::fail(ErrorCode::INTERNAL, "commit ledger update failed");
    res.state = ReservationState::COMMITTED;
    return Result<Reservation>::ok(res);
}

Result<void> Fabric::releaseReservation(const ReservationId& id, const Authority& a) {
    std::unique_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<void>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    auto rit = impl_->reservations.find(id);
    if (rit == impl_->reservations.end())
        return Result<void>::fail(ErrorCode::NOT_FOUND, "reservation not found");
    Reservation& res = rit->second.value;

    if (res.state != ReservationState::RESERVED &&
        res.state != ReservationState::COMMITTED &&
        res.state != ReservationState::ACTIVE)
        return Result<void>::fail(ErrorCode::INVALID_STATE_TRANSITION, "not releasable");

    auto rgit = impl_->regions.find(res.region);
    if (rgit == impl_->regions.end())
        return Result<void>::fail(ErrorCode::WRONG_GENERATION, "region gone");
    RegionRecord& rr = rgit->second;
    if (rr.descriptor.generation != res.regionGeneration)
        return Result<void>::fail(ErrorCode::WRONG_GENERATION, "region generation changed");
    if (res.epoch != impl_->epoch)
        return Result<void>::fail(ErrorCode::WRONG_EPOCH, "reservation epoch stale");

    bool ok = false;
    if (res.state == ReservationState::RESERVED)
        ok = rr.ledger.release(res.bytes);
    else
        ok = rr.ledger.complete(res.bytes);
    if (!ok)
        return Result<void>::fail(ErrorCode::INTERNAL, "release ledger update failed");
    res.state = ReservationState::RELEASED;
    return Result<void>::success();
}

} // namespace memory_expansion_fabric
