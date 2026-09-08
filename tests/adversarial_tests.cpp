#include "test_framework.hpp"
#include "scenario.hpp"
#include "memory_expansion_fabric/capacity.hpp"
#include "memory_expansion_fabric/protocol.hpp"
#include <cstdint>
#include <string>
#include <vector>

using namespace memory_expansion_fabric;
using namespace mef_scenario;

static bool invariantsHold(Fabric& f) {
    auto aud = f.audit();
    return aud.ok() && aud.value().find("FAIL") == std::string::npos;
}

TEST(adversarial_zero_ids_rejected) {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    ProviderDescriptor d = makeProvider(ProviderId(), ProviderGeneration(1), ProviderKind::SYNTHETIC, 4096);
    CHECK(!f.registerProvider(d, a).ok());
    RegionDescriptor r = makeRegion(RegionId(), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), 4096);
    CHECK(!f.registerRegion(r, a).ok());
    CHECK(!f.registerConsumer(ConsumerId(), ConsumerGeneration(1), a).ok());
}

TEST(adversarial_duplicate_rejected) {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    CHECK(f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 4096), a).ok());
    CHECK(!f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 4096), a).ok());  // same gen
    CHECK(!f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(0), ProviderKind::SYNTHETIC, 4096), a).ok()); // gen 0 invalid
}

TEST(adversarial_absurd_capacity) {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, UINT64_MAX), a);
    f.registerRegion(makeRegion(RegionId(1), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), UINT64_MAX), a);
    auto ev = makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), UINT64_MAX,
        HealthState::HEALTHY, true, Locality::LOCAL, 100, 100, WorkerId(), WorkerBootId(), CoordinatorEpoch(1), EvidenceId(1), EvidenceGeneration(1), 1);
    CHECK(f.publishRegionEvidence(ev, a).ok());
    CHECK(invariantsHold(f));
}

TEST(adversarial_ledger_bounds) {
    CapacityLedger led(1000);
    CHECK(led.setOnline(1000));
    CHECK(!led.setOnline(1001));   // exceeds total
    CHECK(led.setOnline(500));     // shrink below total
    CHECK_EQ(led.governedOnline(), 500u);
    CHECK(led.reserve(500));       // free 0
    CHECK(!led.reserve(1));        // underflow
    CHECK(led.invariant());

    // Large-value handling never overflows.
    CapacityLedger big(UINT64_MAX);
    CHECK(big.setOnline(UINT64_MAX));
    CHECK(big.reserve(UINT64_MAX));
    CHECK(!big.reserve(1));
    CHECK(big.commit(UINT64_MAX));
    CHECK(big.complete(UINT64_MAX));
    CHECK_EQ(big.free(), UINT64_MAX);
    CHECK(big.invariant());
}

TEST(adversarial_codec_empty_strings_and_vectors) {
    // RegionEvidence with empty provenance string roundtrips.
    RegionEvidence ev;
    ev.header.provenance = "";
    ev.header.epoch = CoordinatorEpoch(1);
    auto enc = encodeRegionEvidence(ev);
    CHECK(enc.ok());
    auto dec = decodeRegionEvidence(enc.value());
    CHECK(dec.ok());
    CHECK_EQ(dec.value().header.provenance, std::string(""));

    // Empty requirements roundtrips.
    ConsumerRequirements rq;
    auto enc2 = encodeRequirements(rq);
    CHECK(enc2.ok());
    auto dec2 = decodeRequirements(enc2.value());
    CHECK(dec2.ok());
    CHECK(dec2.value().allowedKinds.empty());
}

TEST(adversarial_codec_reject_malformed) {
    // Empty buffer -> INCOMPLETE (need more).
    std::uint8_t empty[1] = {0};
    Frame f2; std::size_t consumed=0; std::string err;
    CHECK(decodeFrame(empty, 0, f2, consumed, err) == FrameDecode::INCOMPLETE);
    // Bad magic.
    std::uint8_t badMagic[16] = {0};
    CHECK(decodeFrame(badMagic, 16, f2, consumed, err) == FrameDecode::ERROR);
    // Unknown frame type.
    auto f = encodeFrame(FrameType::HELLO, {}).value();
    f[6] = 0xFF; f[7] = 0xFF;  // type = 0xFFFF unknown
    CHECK(decodeFrame(f.data(), f.size(), f2, consumed, err) == FrameDecode::ERROR);
    // Truncated payload (declared length > available).
    auto f3 = encodeFrame(FrameType::HELLO, {1,2,3,4}).value();
    CHECK(decodeFrame(f3.data(), f3.size()-2, f2, consumed, err) == FrameDecode::INCOMPLETE);
    // Oversized declared length.
    auto f4 = encodeFrame(FrameType::RESERVE, {}).value();
    f4[8]=0xFF; f4[9]=0xFF; f4[10]=0xFF; f4[11]=0xFF;  // len = 0xFFFFFFFF
    CHECK(decodeFrame(f4.data(), f4.size(), f2, consumed, err) == FrameDecode::ERROR);
    // Checksum mismatch.
    auto f5 = encodeFrame(FrameType::HELLO, {1,2,3}).value();
    f5[12] ^= 0x01;
    CHECK(decodeFrame(f5.data(), f5.size(), f2, consumed, err) == FrameDecode::ERROR);
}

TEST(adversarial_stale_generation_replay) {
    Authority a(CoordinatorEpoch(1));
    Fabric f(CoordinatorEpoch(1));
    f.registerDomain(ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1), "d", OriginClass::SYNTHETIC, FailureDomainId()}, a);
    f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(1), ProviderKind::SYNTHETIC, 4096), a);
    f.registerRegion(makeRegion(RegionId(1), RegionGeneration(1), ProviderId(1), ExpansionDomainId(1), 4096), a);
    // publish current evidence
    f.publishRegionEvidence(makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), 4096, HealthState::HEALTHY, true, Locality::LOCAL, 100, 100, WorkerId(), WorkerBootId(), CoordinatorEpoch(1), EvidenceId(1), EvidenceGeneration(1), 1), a);
    // reincarnate provider gen 1 -> 2
    f.registerProvider(makeProvider(ProviderId(1), ProviderGeneration(2), ProviderKind::SYNTHETIC, 4096), a);
    // stale provider-generation evidence replay must be rejected
    CHECK(!f.publishRegionEvidence(makeEvidence(RegionId(1), RegionGeneration(1), ProviderId(1), ProviderGeneration(1), 4096, HealthState::HEALTHY, true, Locality::LOCAL, 100, 100, WorkerId(), WorkerBootId(), CoordinatorEpoch(1), EvidenceId(2), EvidenceGeneration(2), 2), a).ok());
}

MEF_MAIN()
