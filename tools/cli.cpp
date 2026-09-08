// Memory Expansion Fabric - CLI / inspection tool.
#include "memory_expansion_fabric/fabric.hpp"
#include "memory_expansion_fabric/backend.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mef = memory_expansion_fabric;

namespace {

std::string arg(int argc, char** argv, const std::string& name, const std::string& dflt) {
    for (int i = 1; i < argc - 1; ++i)
        if (std::string(argv[i]) == name) return argv[i+1];
    return dflt;
}
bool has(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == name) return true;
    return false;
}

static mef::Fabric buildFabric(const std::string& backend, mef::Authority& a, mef::ConsumerId& consumer) {
    mef::Fabric f(mef::CoordinatorEpoch(1));
    a = mef::Authority(mef::CoordinatorEpoch(1));
    auto disc = mef::makeDiscovery(backend);
    for (const auto& p : disc->providers()) f.registerProvider(p, a);
    for (const auto& d : disc->domains()) f.registerDomain(d, a);
    for (const auto& r : disc->regions()) f.registerRegion(r, a);
    for (const auto& e : disc->initialEvidence(f.epoch())) f.publishRegionEvidence(e, a);
    consumer = mef::ConsumerId(10);
    f.registerConsumer(consumer, mef::ConsumerGeneration(1), a);
    return f;
}

const char* originName(mef::OriginClass o) {
    switch (o) { case mef::OriginClass::REAL: return "REAL";
                 case mef::OriginClass::SYNTHETIC: return "SYNTHETIC";
                 case mef::OriginClass::UNSUPPORTED: return "UNSUPPORTED";
                 case mef::OriginClass::UNKNOWN: return "UNKNOWN"; }
    return "UNKNOWN";
}
const char* outName(mef::SelectionOutcome o) {
    switch (o) { case mef::SelectionOutcome::EXPANSION_SELECTED: return "EXPANSION_SELECTED";
                 case mef::SelectionOutcome::ALTERNATE_EXPANSION_SELECTED: return "ALTERNATE_EXPANSION_SELECTED";
                 case mef::SelectionOutcome::LOCAL_FALLBACK_SELECTED: return "LOCAL_FALLBACK_SELECTED";
                 case mef::SelectionOutcome::STAGING_REQUIRED: return "STAGING_REQUIRED";
                 case mef::SelectionOutcome::DEFER: return "DEFER";
                 case mef::SelectionOutcome::REJECT: return "REJECT";
                 case mef::SelectionOutcome::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED"; }
    return "?";
}

int cmdDiscover(const std::string& backend) {
    auto disc = mef::makeDiscovery(backend);
    std::printf("backend=%s\n", disc->name().c_str());
    std::printf("origin=%s\n", originName(disc->originClass()));
    std::printf("summary: %s\n", disc->summary().c_str());
    std::printf("--- capability matrix ---\n%s", disc->capabilityReport().c_str());
    std::printf("--- discovered structure ---\n");
    for (const auto& p : disc->providers())
        std::printf("provider %llu gen=%llu kind=%d origin=%s cap=%llu\n",
                    (unsigned long long)p.id.value(), (unsigned long long)p.generation.value(),
                    (int)p.kind, originName(p.origin), (unsigned long long)p.totalCapacityBytes);
    for (const auto& r : disc->regions())
        std::printf("region %llu gen=%llu provider=%llu origin=%s cap=%llu\n",
                    (unsigned long long)r.id.value(), (unsigned long long)r.generation.value(),
                    (unsigned long long)r.provider.value(), originName(r.origin),
                    (unsigned long long)r.totalCapacityBytes);
    return 0;
}

int cmdInspect(const std::string& backend) {
    mef::Authority a(mef::CoordinatorEpoch(1)); mef::ConsumerId c;
    mef::Fabric f = buildFabric(backend, a, c);
    auto snap = f.snapshot();
    std::printf("epoch=%llu\n", (unsigned long long)snap.epoch.value());
    for (const auto& r : snap.regions)
        std::printf("region %llu life=%d ev=%d online=%llu free=%llu reserved=%llu committed=%llu draining=%llu unavailable=%llu reach=%d\n",
                    (unsigned long long)r.id.value(), (int)r.lifecycle, (int)r.evidenceStatus,
                    (unsigned long long)r.onlineBytes, (unsigned long long)r.freeBytes,
                    (unsigned long long)r.reservedBytes, (unsigned long long)r.committedBytes,
                    (unsigned long long)r.drainingBytes, (unsigned long long)r.unavailableBytes,
                    r.reachable?1:0);
    auto aud = f.audit();
    std::printf("--- audit ---\n%s", aud.ok()?aud.value().c_str():"audit failed\n");
    return 0;
}

int cmdDemo(const std::string& backend) {
    mef::Authority a(mef::CoordinatorEpoch(1)); mef::ConsumerId c;
    mef::Fabric f = buildFabric(backend, a, c);

    mef::ConsumerRequirements req;
    req.consumer = c; req.consumerGeneration = mef::ConsumerGeneration(1);
    req.requiredBytes = 4ull * 1024 * 1024 * 1024; req.acceleratorAccessible = true;
    auto sel = f.select(req, a);
    if (sel.ok()) {
        std::printf("select: outcome=%s ranked=%llu rejected=%llu tie=%s\n",
                    outName(sel.value().outcome), (unsigned long long)sel.value().ranked.size(),
                    (unsigned long long)sel.value().rejected.size(), sel.value().tieBreakRule.c_str());
        if (!sel.value().ranked.empty())
            std::printf("  top: region=%llu provider=%llu score=%f\n",
                        (unsigned long long)sel.value().ranked[0].region.value(),
                        (unsigned long long)sel.value().ranked[0].provider.value(),
                        sel.value().ranked[0].score);
        for (const auto& rj : sel.value().rejected)
            std::printf("  rejected region=%llu reason=%d\n", (unsigned long long)rj.region.value(), (int)rj.reason);
    } else {
        std::printf("select error: %s\n", sel.error().message.c_str());
    }

    auto rid = f.requestReservation(req, a);
    if (rid.ok()) {
        std::printf("reserve: id=%llu ok\n", (unsigned long long)rid.value().value());
        auto cr = f.commitReservation(rid.value(), a);
        std::printf("commit: %s state=%d\n", cr.ok()?"ok":"failed", cr.ok()?(int)cr.value().state:-1);
        std::printf("release: %s\n", f.releaseReservation(rid.value(), a).ok()?"ok":"failed");
    } else {
        std::printf("reserve failed: %s\n", rid.error().message.c_str());
    }
    std::printf("--- audit ---\n%s", f.audit().ok()?f.audit().value().c_str():"audit failed\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string backend = arg(argc, argv, "--backend", "synthetic");
    if (argc < 2) {
        std::printf("usage: mef_cli <discover|inspect|demo> [--backend system|synthetic|unsupported]\n");
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "discover") return cmdDiscover(backend);
    if (cmd == "inspect") return cmdInspect(backend);
    if (cmd == "demo") return cmdDemo(backend);
    std::printf("unknown command: %s\n", cmd.c_str());
    return 1;
}
