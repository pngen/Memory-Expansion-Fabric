#include "fabric_impl.hpp"
#include <algorithm>
#include <limits>

namespace memory_expansion_fabric {

// ---------------------------------------------------------------------------
// Hard eligibility. Returns ELIGIBLE or a typed hard-rejection reason.
// Caller holds stateMtx (read). An ineligible candidate never wins.
// ---------------------------------------------------------------------------
EligibilityReason Fabric::Impl::evaluateRegionEligibility(const ConsumerRequirements& req,
                                                          const RegionRecord& rr) const {
    auto pit = providers.find(rr.descriptor.provider);
    if (pit == providers.end()) return EligibilityReason::OFFLINE;
    const auto& pr = pit->second;
    if (pr.lifecycle == Lifecycle::RETIRED || pr.lifecycle == Lifecycle::FAILED)
        return EligibilityReason::OFFLINE;

    // Lifecycle gate.
    if (rr.lifecycle == RegionLifecycle::RETIRED) return EligibilityReason::OFFLINE;
    if (rr.lifecycle == RegionLifecycle::DRAINING)
        return EligibilityReason::DEGRADED_NOT_ALLOWED;
    bool allowDegraded = req.allowDegraded;
    if (!req.policy.null()) {
        auto polit = policies.find(req.policy);
        if (polit != policies.end() && polit->second.descriptor.allowDegraded)
            allowDegraded = true;
    }
    if (rr.lifecycle == RegionLifecycle::DEGRADED && !allowDegraded)
        return EligibilityReason::DEGRADED_NOT_ALLOWED;
    if (rr.lifecycle != RegionLifecycle::ONLINE && rr.lifecycle != RegionLifecycle::DEGRADED)
        return EligibilityReason::REVALIDATION_REQUIRED;

    // Evidence must be current (fresh) and reachable.
    if (!rr.hasEvidence) return EligibilityReason::STALE_EVIDENCE;
    if (rr.evidenceStatus != EvidenceStatus::CURRENT) return EligibilityReason::STALE_EVIDENCE;
    if (rr.health == HealthState::FAILED) return EligibilityReason::REVALIDATION_REQUIRED;
    if (rr.health == HealthState::DEGRADED && !allowDegraded)
        return EligibilityReason::DEGRADED_NOT_ALLOWED;
    if (!rr.reachable) return EligibilityReason::UNREACHABLE;

    // Capacity.
    if (rr.ledger.free() < req.requiredBytes) return EligibilityReason::INSUFFICIENT_CAPACITY;

    // Capability checks. UNKNOWN and REVALIDATION_REQUIRED both fail closed.
    auto cap = [&](CapabilityKey key) -> int {
        CapabilityStatus s = pr.descriptor.capabilities.get(key);
        if (s == CapabilityStatus::UNKNOWN) s = rr.descriptor.staticCapabilities.get(key);
        if (s == CapabilityStatus::SUPPORTED) return 0;
        if (s == CapabilityStatus::UNSUPPORTED) return 1;
        return 2; // UNKNOWN / REVALIDATION_REQUIRED
    };
    if (req.cpuAccessible) {
        int c = cap(CapabilityKey::CPU_ACCESSIBLE);
        if (c == 1) return EligibilityReason::CAPABILITY_UNSUPPORTED;
        if (c == 2) return EligibilityReason::CAPABILITY_UNKNOWN;
    }
    if (req.acceleratorAccessible) {
        int c = cap(CapabilityKey::ACCELERATOR_ACCESSIBLE);
        if (c == 1) return EligibilityReason::CAPABILITY_UNSUPPORTED;
        if (c == 2) return EligibilityReason::CAPABILITY_UNKNOWN;
    }
    if (req.directAccessRequired) {
        int c = cap(CapabilityKey::DIRECT_ACCESS);
        if (c == 1) return EligibilityReason::CAPABILITY_UNSUPPORTED;
        if (c == 2) return EligibilityReason::CAPABILITY_UNKNOWN;
    }
    if (req.persistent) {
        if (pr.descriptor.persistence != Persistence::PERSISTENT)
            return EligibilityReason::PERSISTENCE_REQUIRED;
    }

    // Alignment / granularity. A request smaller than one granularity unit is
    // satisfiable; a larger request must be a whole number of units.
    if (req.alignmentBytes > rr.descriptor.alignmentBytes)
        return EligibilityReason::INCOMPATIBLE;
    if (req.requiredBytes != 0 && rr.descriptor.granularityBytes != 0 &&
        req.requiredBytes > rr.descriptor.granularityBytes &&
        req.requiredBytes % rr.descriptor.granularityBytes != 0)
        return EligibilityReason::INCOMPATIBLE;

    // Latency / bandwidth. Unknown values with a limit fail closed.
    if (req.maxLatencyNs > 0) {
        if (rr.latencyNs == 0) return EligibilityReason::LATENCY_LIMIT_EXCEEDED;
        if (rr.latencyNs > req.maxLatencyNs) return EligibilityReason::LATENCY_LIMIT_EXCEEDED;
    }
    if (req.minBandwidth > 0) {
        if (rr.bandwidthBytesPerSec == 0) return EligibilityReason::BANDWIDTH_INSUFFICIENT;
        if (rr.bandwidthBytesPerSec < req.minBandwidth)
            return EligibilityReason::BANDWIDTH_INSUFFICIENT;
    }

    // Provider-kind allow/forbid.
    if (!req.allowedKinds.empty()) {
        bool ok = false;
        for (auto k : req.allowedKinds) if (k == pr.descriptor.kind) { ok = true; break; }
        if (!ok) return EligibilityReason::INCOMPATIBLE;
    }
    for (auto k : req.forbiddenKinds) if (k == pr.descriptor.kind)
        return EligibilityReason::INCOMPATIBLE;

    // Locality constraint (hard when specified).
    if (req.localityPreference != Locality::UNKNOWN) {
        Locality l = rr.latest.locality;
        if (req.localityPreference == Locality::LOCAL && l != Locality::LOCAL)
            return EligibilityReason::TOO_REMOTE;
        if (req.localityPreference == Locality::REMOTE &&
            (l == Locality::LOCAL || l == Locality::UNKNOWN))
            return EligibilityReason::TOO_REMOTE;
        if (req.localityPreference == Locality::FABRIC && l != Locality::FABRIC)
            return EligibilityReason::TOO_REMOTE;
    }

    // Failure-domain conflict.
    if (!req.forbiddenFailureDomain.null() &&
        pr.descriptor.failureDomain == req.forbiddenFailureDomain)
        return EligibilityReason::FAILURE_DOMAIN_CONFLICT;

    // Policy presence.
    if (!req.policy.null() && policies.find(req.policy) == policies.end())
        return EligibilityReason::POLICY_DENIED;

    return EligibilityReason::ELIGIBLE;
}

namespace {
double localityScore(Locality l) {
    switch (l) {
        case Locality::LOCAL: return 1.0;
        case Locality::FABRIC: return 0.7;
        case Locality::REMOTE: return 0.5;
        case Locality::UNKNOWN:
        default: return 0.3;
    }
}
double healthScore(HealthState h) {
    switch (h) {
        case HealthState::HEALTHY: return 1.0;
        case HealthState::DEGRADED: return 0.6;
        case HealthState::FAILED: return 0.0;
        case HealthState::UNKNOWN:
        default: return 0.5;
    }
}
double accessScore(AccessKind k) {
    switch (k) {
        case AccessKind::DIRECT: return 1.0;
        case AccessKind::STAGED: return 0.6;
        case AccessKind::MEDIATED: return 0.4;
        case AccessKind::UNKNOWN:
        default: return 0.4;
    }
}
AccessKind bestAccess(const RegionRecord& rr, const ConsumerId& consumer,
                           const CapabilityModel& providerCaps) {
    // Prefer a direct attachment for this consumer; otherwise fall back to the
    // provider's declared access capability.
    AccessKind best = AccessKind::UNKNOWN;
    for (const auto& att : rr.attachments) {
        if (!att.consumer.null() && att.consumer != consumer) continue;
        if (att.access == AccessKind::DIRECT) return AccessKind::DIRECT;
        if (att.access != AccessKind::UNKNOWN && best == AccessKind::UNKNOWN) best = att.access;
    }
    if (best != AccessKind::UNKNOWN) return best;
    if (providerCaps.get(CapabilityKey::DIRECT_ACCESS) == CapabilityStatus::SUPPORTED)
        return AccessKind::DIRECT;
    if (providerCaps.get(CapabilityKey::STAGING_REQUIRED) == CapabilityStatus::SUPPORTED)
        return AccessKind::STAGED;
    return AccessKind::UNKNOWN;
}
} // namespace

SelectionResult Fabric::Impl::evaluateLocked(const ConsumerRequirements& req) {
    SelectionResult sel;
    sel.id = selectionIdAlloc.next<SelectionId>();
    sel.generation = selectionGenAlloc.next<SelectionGeneration>();
    sel.tieBreakRule = "score desc, then region id, provider id, pool id";

    struct Cand {
        RegionId region;
        ProviderId provider;
        PoolId pool;
        std::uint64_t bytes;
        std::uint64_t free;
        std::uint64_t latencyNs;
        std::uint64_t bandwidth;
        Locality locality;
        HealthState health;
        AccessKind access;
        std::vector<RankingFactor> factors;
        double score = 0.0;
    };
    std::vector<Cand> eligible;
    std::vector<CandidateAssessment> rejected;
    bool anyRevalidation = false;

    for (const auto& [rid, rr] : regions) {
        if (!req.preferredPool.null()) {
            bool member = false;
            for (const auto& [pid, pgen] : rr.poolMemberships) {
                (void)pgen;
                if (pid == req.preferredPool) { member = true; break; }
            }
            if (!member) continue;
        }
        EligibilityReason reason = evaluateRegionEligibility(req, rr);
        if (reason != EligibilityReason::ELIGIBLE) {
            if (reason == EligibilityReason::REVALIDATION_REQUIRED ||
                reason == EligibilityReason::STALE_EVIDENCE)
                anyRevalidation = true;
            rejected.push_back(CandidateAssessment{rid, rr.descriptor.provider,
                                                   req.preferredPool, reason, 0.0});
            continue;
        }
        Cand c;
        c.region = rid;
        c.provider = rr.descriptor.provider;
        c.pool = req.preferredPool;
        c.bytes = req.requiredBytes;
        c.free = rr.ledger.free();
        c.latencyNs = rr.latencyNs;
        c.bandwidth = rr.bandwidthBytesPerSec;
        c.locality = rr.latest.locality;
        c.health = rr.health;
        auto pitCaps = providers.find(rr.descriptor.provider);
        c.access = bestAccess(rr, req.consumer,
                              pitCaps != providers.end()
                                  ? pitCaps->second.descriptor.capabilities
                                  : CapabilityModel());
        eligible.push_back(c);
    }

    double maxLatency = 0, maxBw = 0;
    for (const auto& c : eligible) {
        if (c.latencyNs > maxLatency) maxLatency = static_cast<double>(c.latencyNs);
        if (c.bandwidth > maxBw) maxBw = static_cast<double>(c.bandwidth);
    }
    const double kW = 0.30, kLat = 0.20, kBw = 0.20, kHead = 0.15, kAcc = 0.15;
    for (auto& c : eligible) {
        double loc = localityScore(c.locality);
        double lat = maxLatency > 0 ? (1.0 - (static_cast<double>(c.latencyNs) / maxLatency)) : 1.0;
        if (c.latencyNs == 0) lat = 0.0;
        double bw = maxBw > 0 ? (static_cast<double>(c.bandwidth) / maxBw) : 1.0;
        double head = req.requiredBytes > 0
            ? (static_cast<double>(c.free) /
               (static_cast<double>(req.requiredBytes) * (1.0 + req.reserveHeadroomFactor)))
            : 1.0;
        head = head > 1.0 ? 1.0 : head;
        double acc = accessScore(c.access);
        double hlth = healthScore(c.health);
        (void)hlth;

        c.factors.push_back({"locality", loc, loc, kW});
        c.factors.push_back({"latency", static_cast<double>(c.latencyNs), lat, kLat});
        c.factors.push_back({"bandwidth", static_cast<double>(c.bandwidth), bw, kBw});
        c.factors.push_back({"headroom", static_cast<double>(c.free), head, kHead});
        c.factors.push_back({"access", static_cast<double>(c.access), acc, kAcc});
        c.factors.push_back({"health", static_cast<double>(c.health), hlth, 0.05});
        c.score = kW * loc + kLat * lat + kBw * bw + kHead * head + kAcc * acc + 0.05 * hlth;
    }
    std::stable_sort(eligible.begin(), eligible.end(),
        [](const Cand& x, const Cand& y) {
            if (x.score != y.score) return x.score > y.score;
            if (x.region.value() != y.region.value()) return x.region.value() < y.region.value();
            if (x.provider.value() != y.provider.value()) return x.provider.value() < y.provider.value();
            return x.pool.value() < y.pool.value();
        });

    for (const auto& c : eligible) {
        RankedCandidate rc;
        rc.region = c.region;
        rc.provider = c.provider;
        rc.pool = c.pool;
        rc.bytes = c.bytes;
        rc.score = c.score;
        rc.factors = c.factors;
        sel.ranked.push_back(std::move(rc));
    }
    sel.rejected = std::move(rejected);

    if (!sel.ranked.empty()) {
        auto top = regions.find(sel.ranked[0].region);
        AccessKind topAccess = AccessKind::UNKNOWN;
        if (top != regions.end()) {
            auto pc = providers.find(top->second.descriptor.provider);
            topAccess = bestAccess(top->second, req.consumer,
                                   pc != providers.end()
                                       ? pc->second.descriptor.capabilities
                                       : CapabilityModel());
        }
        sel.outcome = (topAccess == AccessKind::STAGED)
                          ? SelectionOutcome::STAGING_REQUIRED
                          : SelectionOutcome::EXPANSION_SELECTED;
    } else if (!req.expansionRequired && req.allowLocalFallback) {
        sel.outcome = SelectionOutcome::LOCAL_FALLBACK_SELECTED;
    } else if (anyRevalidation) {
        sel.outcome = SelectionOutcome::REVALIDATION_REQUIRED;
    } else {
        sel.outcome = SelectionOutcome::REJECT;
    }
    return sel;
}

Result<SelectionResult> Fabric::select(const ConsumerRequirements& req, const Authority& a) {
    std::shared_lock lk(impl_->stateMtx);
    if (!impl_->authorityAcceptableLocked(a))
        return Result<SelectionResult>::fail(ErrorCode::STALE_AUTHORITY, "stale authority");
    if (req.requiredBytes == 0)
        return Result<SelectionResult>::fail(ErrorCode::INVALID_ARGUMENT, "select requires bytes>0");
    return Result<SelectionResult>::ok(impl_->evaluateLocked(req));
}

} // namespace memory_expansion_fabric
