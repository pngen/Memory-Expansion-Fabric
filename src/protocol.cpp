#include "memory_expansion_fabric/protocol.hpp"
#include <cstring>
#include <cstdint>

namespace memory_expansion_fabric {

namespace {
constexpr std::uint8_t kMagic[4] = {'M', 'E', 'F', 'W'};
constexpr std::uint32_t kMaxPayload = 1024u * 1024u;
constexpr std::uint32_t kMaxCount = 8192u;
constexpr std::uint32_t kMaxString = 1024u;

std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

class Reader {
public:
    Reader(const std::uint8_t* p, std::size_t len) : p_(p), len_(len), pos_(0) {}
    bool u8(std::uint8_t& v){ if(!have(1))return false; v=p_[pos_++]; return true; }
    bool u16(std::uint16_t& v){ if(!have(2))return false; v=(uint16_t)p_[pos_]|((uint16_t)p_[pos_+1]<<8); pos_+=2; return true; }
    bool u32(std::uint32_t& v){ if(!have(4))return false; v=(uint32_t)p_[pos_]|((uint32_t)p_[pos_+1]<<8)|((uint32_t)p_[pos_+2]<<16)|((uint32_t)p_[pos_+3]<<24); pos_+=4; return true; }
    bool u64(std::uint64_t& v){ uint32_t lo,hi; if(!u32(lo)||!u32(hi))return false; v=(uint64_t)lo|((uint64_t)hi<<32); return true; }
    bool f64(double& v){ uint64_t u; if(!u64(u))return false; std::memcpy(&v,&u,8); return true; }
    bool str(std::string& s){ std::uint32_t n; if(!u32(n))return false; if(n>kMaxString)return false; if(!have(n))return false; s.assign((const char*)(p_+pos_), n); pos_+=n; return true; }
    bool finish() const { return pos_ == len_; }
private:
    bool have(std::size_t n) const { return n <= len_ - pos_; }
    const std::uint8_t* p_; std::size_t len_, pos_;
};
class Writer {
public:
    void u8(uint8_t v){ b_.push_back(v); }
    void u16(uint16_t v){ b_.push_back(v&0xFFu); b_.push_back((v>>8)&0xFFu); }
    void u32(uint32_t v){ b_.push_back(v&0xFFu); b_.push_back((v>>8)&0xFFu); b_.push_back((v>>16)&0xFFu); b_.push_back((v>>24)&0xFFu); }
    void u64(uint64_t v){ u32((uint32_t)v); u32((uint32_t)(v>>32)); }
    void f64(double v){ uint64_t u; std::memcpy(&u,&v,8); u64(u); }
    void str(const std::string& s){ u32((uint32_t)s.size()); b_.insert(b_.end(), s.begin(), s.end()); }
    const std::vector<uint8_t>& data() const { return b_; }
private:
    std::vector<uint8_t> b_;
};

template <typename T>
static Result<T> failCodec(const char* what) {
    return Result<T>::fail(ErrorCode::CORRUPT, std::string("decode: ") + what);
}
} // namespace

bool isKnownFrameType(std::uint16_t t) {
    switch (static_cast<FrameType>(t)) {
        case FrameType::HELLO: case FrameType::HELLO_ACK: case FrameType::HEARTBEAT:
        case FrameType::BYE: case FrameType::PUBLISH_EVIDENCE: case FrameType::PUBLISH_ATTACHMENT:
        case FrameType::SELECT: case FrameType::SELECT_RESP: case FrameType::RESERVE:
        case FrameType::RESERVE_RESP: case FrameType::COMMIT: case FrameType::COMMIT_RESP:
        case FrameType::RELEASE: case FrameType::RELEASE_RESP: case FrameType::INSPECT:
        case FrameType::INSPECT_RESP: case FrameType::AUDIT: case FrameType::AUDIT_RESP:
        case FrameType::SPAWN_WORKER: case FrameType::SPAWN_WORKER_RESP:
        case FrameType::TERMINATE_WORKER: case FrameType::TERMINATE_WORKER_RESP:
        case FrameType::SAVE: case FrameType::SAVE_RESP:
            return true;
    }
    return false;
}

Result<std::vector<std::uint8_t>> encodeFrame(FrameType type, const std::vector<std::uint8_t>& payload) {
    if (payload.size() > kMaxPayload)
        return Result<std::vector<std::uint8_t>>::fail(ErrorCode::FRAME_TOO_LARGE, "payload too large");
    Writer w;
    for (auto b : kMagic) w.u8(b);
    w.u16(kProtocolVersion);
    w.u16(static_cast<std::uint16_t>(type));
    w.u32((uint32_t)payload.size());
    w.u32(crc32(payload.data(), payload.size()));
    std::vector<uint8_t> out = w.data();
    out.insert(out.end(), payload.begin(), payload.end());
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

FrameDecode decodeFrame(const std::uint8_t* data, std::size_t len, Frame& out,
                        std::size_t& consumed, std::string& err) {
    consumed = 0;
    constexpr std::size_t kHeader = 16;
    if (len < kHeader) return FrameDecode::INCOMPLETE;
    if (std::memcmp(data, kMagic, 4) != 0) { err = "bad magic"; return FrameDecode::ERROR; }
    std::uint16_t ver = (uint16_t)data[4] | ((uint16_t)data[5] << 8);
    if (ver != kProtocolVersion) { err = "bad version"; return FrameDecode::ERROR; }
    std::uint16_t t = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
    if (!isKnownFrameType(t)) { err = "unknown frame type"; return FrameDecode::ERROR; }
    std::uint32_t plen = (uint32_t)data[8] | ((uint32_t)data[9] << 8) |
                         ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
    if (plen > kMaxPayload) { err = "oversized frame"; return FrameDecode::ERROR; }
    if (len < kHeader + plen) return FrameDecode::INCOMPLETE;
    std::uint32_t crc = (uint32_t)data[12] | ((uint32_t)data[13] << 8) |
                        ((uint32_t)data[14] << 16) | ((uint32_t)data[15] << 24);
    if (crc32(data + kHeader, plen) != crc) { err = "checksum mismatch"; return FrameDecode::ERROR; }
    out.type = static_cast<FrameType>(t);
    out.payload.assign(data + kHeader, data + kHeader + plen);
    consumed = kHeader + plen;
    return FrameDecode::OK;
}

// --- RegionEvidence codec ---------------------------------------------------
Result<std::vector<std::uint8_t>> encodeRegionEvidence(const RegionEvidence& e) {
    Writer w;
    w.u64(e.header.id.value()); w.u64(e.header.generation.value());
    w.u64(e.header.sourceProvider.value()); w.u64(e.header.sourceProviderGeneration.value());
    w.u64(e.header.region.value()); w.u64(e.header.regionGeneration.value());
    w.u64(e.header.worker.value()); w.u64(e.header.boot.value()); w.u64(e.header.epoch.value());
    w.u64(e.header.timestampMs); w.u64(e.header.sequence); w.str(e.header.provenance); w.f64(e.header.confidence);
    w.u8((uint8_t)e.health); w.u8(e.reachable?1:0); w.u64(e.onlineCapacityBytes);
    w.u64(e.latencyNs); w.u64(e.bandwidthBytesPerSec); w.u8((uint8_t)e.locality); w.u64(e.nodeId);
    w.u8((uint8_t)e.status);
    return Result<std::vector<std::uint8_t>>::ok(w.data());
}
Result<RegionEvidence> decodeRegionEvidence(const std::vector<std::uint8_t>& b) {
    Reader r(b.data(), b.size());
    RegionEvidence e;
    std::uint64_t v; std::uint8_t u; std::string s;
    if(!r.u64(v))return failCodec<RegionEvidence>("id");
    e.header.id=EvidenceId(v);
    if(!r.u64(v))return failCodec<RegionEvidence>("gen");
    e.header.generation=EvidenceGeneration(v);
    if(!r.u64(v))return failCodec<RegionEvidence>("srcprov");
    e.header.sourceProvider=ProviderId(v);
    if(!r.u64(v))return failCodec<RegionEvidence>("srcpgen");
    e.header.sourceProviderGeneration=ProviderGeneration(v);
    if(!r.u64(v))return failCodec<RegionEvidence>("region");
    e.header.region=RegionId(v);
    if(!r.u64(v))return failCodec<RegionEvidence>("rgen");
    e.header.regionGeneration=RegionGeneration(v);
    if(!r.u64(v))return failCodec<RegionEvidence>("worker");
    e.header.worker=WorkerId(v);
    if(!r.u64(v))return failCodec<RegionEvidence>("boot");
    e.header.boot=WorkerBootId(v);
    if(!r.u64(v))return failCodec<RegionEvidence>("epoch");
    e.header.epoch=CoordinatorEpoch(v);
    if(!r.u64(e.header.timestampMs))return failCodec<RegionEvidence>("ts");
    if(!r.u64(e.header.sequence))return failCodec<RegionEvidence>("seq");
    if(!r.str(e.header.provenance))return failCodec<RegionEvidence>("prov");
    if(!r.f64(e.header.confidence))return failCodec<RegionEvidence>("conf");
    if(!r.u8(u))return failCodec<RegionEvidence>("health"); e.health=(HealthState)u;
    if(!r.u8(u))return failCodec<RegionEvidence>("reach"); e.reachable=u!=0;
    if(!r.u64(e.onlineCapacityBytes))return failCodec<RegionEvidence>("online");
    if(!r.u64(e.latencyNs))return failCodec<RegionEvidence>("lat");
    if(!r.u64(e.bandwidthBytesPerSec))return failCodec<RegionEvidence>("bw");
    if(!r.u8(u))return failCodec<RegionEvidence>("loc"); e.locality=(Locality)u;
    if(!r.u64(e.nodeId))return failCodec<RegionEvidence>("node");
    if(!r.u8(u))return failCodec<RegionEvidence>("status"); e.status=(EvidenceStatus)u;
    if(!r.finish())return failCodec<RegionEvidence>("trailing");
    return Result<RegionEvidence>::ok(std::move(e));
}

// --- ConsumerRequirements codec ----------------------------------------------
Result<std::vector<std::uint8_t>> encodeRequirements(const ConsumerRequirements& r) {
    Writer w;
    w.u64(r.consumer.value()); w.u64(r.consumerGeneration.value());
    w.u64(r.requiredBytes); w.u64(r.alignmentBytes);
    w.u8(r.cpuAccessible?1:0); w.u8(r.acceleratorAccessible?1:0);
    w.u8(r.directAccessRequired?1:0); w.u8(r.stagingAcceptable?1:0); w.u8(r.persistent?1:0);
    w.u64(r.maxLatencyNs); w.u64(r.minBandwidth); w.u8((uint8_t)r.localityPreference);
    w.u32((uint32_t)r.allowedKinds.size());
    for (auto k : r.allowedKinds) w.u8((uint8_t)k);
    w.u32((uint32_t)r.forbiddenKinds.size());
    for (auto k : r.forbiddenKinds) w.u8((uint8_t)k);
    w.u8(r.failureDomainIsolationRequired?1:0); w.u64(r.forbiddenFailureDomain.value());
    w.u8(r.freshEvidenceRequired?1:0); w.u8(r.allowDegraded?1:0);
    w.u8(r.allowLocalFallback?1:0); w.u8(r.expansionRequired?1:0); w.u8(r.allowStaging?1:0);
    w.u64(r.policy.value()); w.u64(r.preferredPool.value()); w.f64(r.reserveHeadroomFactor);
    return Result<std::vector<std::uint8_t>>::ok(w.data());
}
Result<ConsumerRequirements> decodeRequirements(const std::vector<std::uint8_t>& b) {
    Reader r(b.data(), b.size());
    ConsumerRequirements c;
    std::uint64_t v; std::uint8_t u; std::uint32_t n;
    if(!r.u64(v))return failCodec<ConsumerRequirements>("consumer"); c.consumer=ConsumerId(v);
    if(!r.u64(v))return failCodec<ConsumerRequirements>("cgen"); c.consumerGeneration=ConsumerGeneration(v);
    if(!r.u64(c.requiredBytes))return failCodec<ConsumerRequirements>("bytes");
    if(!r.u64(c.alignmentBytes))return failCodec<ConsumerRequirements>("align");
    if(!r.u8(u))return failCodec<ConsumerRequirements>("cpu"); c.cpuAccessible=u!=0;
    if(!r.u8(u))return failCodec<ConsumerRequirements>("acc"); c.acceleratorAccessible=u!=0;
    if(!r.u8(u))return failCodec<ConsumerRequirements>("direct"); c.directAccessRequired=u!=0;
    if(!r.u8(u))return failCodec<ConsumerRequirements>("staging"); c.stagingAcceptable=u!=0;
    if(!r.u8(u))return failCodec<ConsumerRequirements>("pers"); c.persistent=u!=0;
    if(!r.u64(c.maxLatencyNs))return failCodec<ConsumerRequirements>("maxlat");
    if(!r.u64(c.minBandwidth))return failCodec<ConsumerRequirements>("minbw");
    if(!r.u8(u))return failCodec<ConsumerRequirements>("loc"); c.localityPreference=(Locality)u;
    if(!r.u32(n))return failCodec<ConsumerRequirements>("allowed"); if(n>kMaxCount)return failCodec<ConsumerRequirements>("allowed");
    for(std::uint32_t i=0;i<n;++i){ if(!r.u8(u))return failCodec<ConsumerRequirements>("allowed"); c.allowedKinds.push_back((ProviderKind)u); }
    if(!r.u32(n))return failCodec<ConsumerRequirements>("forbidden"); if(n>kMaxCount)return failCodec<ConsumerRequirements>("forbidden");
    for(std::uint32_t i=0;i<n;++i){ if(!r.u8(u))return failCodec<ConsumerRequirements>("forbidden"); c.forbiddenKinds.push_back((ProviderKind)u); }
    if(!r.u8(u))return failCodec<ConsumerRequirements>("fdi"); c.failureDomainIsolationRequired=u!=0;
    if(!r.u64(v))return failCodec<ConsumerRequirements>("fdom"); c.forbiddenFailureDomain=FailureDomainId(v);
    if(!r.u8(u))return failCodec<ConsumerRequirements>("fresh"); c.freshEvidenceRequired=u!=0;
    if(!r.u8(u))return failCodec<ConsumerRequirements>("degraded"); c.allowDegraded=u!=0;
    if(!r.u8(u))return failCodec<ConsumerRequirements>("fallback"); c.allowLocalFallback=u!=0;
    if(!r.u8(u))return failCodec<ConsumerRequirements>("expansion"); c.expansionRequired=u!=0;
    if(!r.u8(u))return failCodec<ConsumerRequirements>("allowstaging"); c.allowStaging=u!=0;
    if(!r.u64(v))return failCodec<ConsumerRequirements>("policy"); c.policy=PolicyId(v);
    if(!r.u64(v))return failCodec<ConsumerRequirements>("pool"); c.preferredPool=PoolId(v);
    if(!r.f64(c.reserveHeadroomFactor))return failCodec<ConsumerRequirements>("headroom");
    if(!r.finish())return failCodec<ConsumerRequirements>("trailing");
    return Result<ConsumerRequirements>::ok(std::move(c));
}

// --- SelectionResult codec ----------------------------------------------------
Result<std::vector<std::uint8_t>> encodeSelection(const SelectionResult& s) {
    Writer w;
    w.u64(s.id.value()); w.u64(s.generation.value()); w.u8((uint8_t)s.outcome); w.str(s.tieBreakRule);
    w.u32((uint32_t)s.ranked.size());
    for (const auto& rc : s.ranked) {
        w.u64(rc.region.value()); w.u64(rc.provider.value()); w.u64(rc.pool.value());
        w.u64(rc.bytes); w.f64(rc.score);
        w.u32((uint32_t)rc.factors.size());
        for (const auto& f : rc.factors) { w.str(f.name); w.f64(f.raw); w.f64(f.normalized); w.f64(f.weight); }
    }
    w.u32((uint32_t)s.rejected.size());
    for (const auto& c : s.rejected) { w.u64(c.region.value()); w.u64(c.provider.value()); w.u64(c.pool.value()); w.u8((uint8_t)c.reason); w.f64(c.score); }
    return Result<std::vector<std::uint8_t>>::ok(w.data());
}
Result<SelectionResult> decodeSelection(const std::vector<std::uint8_t>& b) {
    Reader r(b.data(), b.size());
    SelectionResult s;
    std::uint64_t v; std::uint8_t u; std::uint32_t n;
    if(!r.u64(v))return failCodec<SelectionResult>("id"); s.id=SelectionId(v);
    if(!r.u64(v))return failCodec<SelectionResult>("gen"); s.generation=SelectionGeneration(v);
    if(!r.u8(u))return failCodec<SelectionResult>("outcome"); s.outcome=(SelectionOutcome)u;
    if(!r.str(s.tieBreakRule))return failCodec<SelectionResult>("tie");
    if(!r.u32(n))return failCodec<SelectionResult>("ranked"); if(n>kMaxCount)return failCodec<SelectionResult>("ranked");
    for(std::uint32_t i=0;i<n;++i){
        RankedCandidate rc;
        if(!r.u64(v))return failCodec<SelectionResult>("rregion"); rc.region=RegionId(v);
        if(!r.u64(v))return failCodec<SelectionResult>("rprov"); rc.provider=ProviderId(v);
        if(!r.u64(v))return failCodec<SelectionResult>("rpool"); rc.pool=PoolId(v);
        if(!r.u64(rc.bytes))return failCodec<SelectionResult>("rbytes");
        if(!r.f64(rc.score))return failCodec<SelectionResult>("rscore");
        std::uint32_t fc; if(!r.u32(fc))return failCodec<SelectionResult>("factors"); if(fc>kMaxCount)return failCodec<SelectionResult>("factors");
        for(std::uint32_t j=0;j<fc;++j){ RankingFactor f; if(!r.str(f.name))return failCodec<SelectionResult>("fname"); if(!r.f64(f.raw))return failCodec<SelectionResult>("fraw"); if(!r.f64(f.normalized))return failCodec<SelectionResult>("fnorm"); if(!r.f64(f.weight))return failCodec<SelectionResult>("fweight"); rc.factors.push_back(std::move(f)); }
        s.ranked.push_back(std::move(rc));
    }
    if(!r.u32(n))return failCodec<SelectionResult>("rejected"); if(n>kMaxCount)return failCodec<SelectionResult>("rejected");
    for(std::uint32_t i=0;i<n;++i){
        CandidateAssessment ca;
        if(!r.u64(v))return failCodec<SelectionResult>("cregion"); ca.region=RegionId(v);
        if(!r.u64(v))return failCodec<SelectionResult>("cprov"); ca.provider=ProviderId(v);
        if(!r.u64(v))return failCodec<SelectionResult>("cpool"); ca.pool=PoolId(v);
        if(!r.u8(u))return failCodec<SelectionResult>("creason"); ca.reason=(EligibilityReason)u;
        if(!r.f64(ca.score))return failCodec<SelectionResult>("cscore");
        s.rejected.push_back(std::move(ca));
    }
    if(!r.finish())return failCodec<SelectionResult>("trailing");
    return Result<SelectionResult>::ok(std::move(s));
}

// --- Reservation codec ---------------------------------------------------------
Result<std::vector<std::uint8_t>> encodeReservation(const Reservation& r) {
    Writer w;
    w.u64(r.id.value()); w.u64(r.generation.value()); w.u64(r.consumer.value()); w.u64(r.consumerGeneration.value());
    w.u64(r.provider.value()); w.u64(r.providerGeneration.value()); w.u64(r.region.value()); w.u64(r.regionGeneration.value());
    w.u64(r.pool.value()); w.u64(r.poolGeneration.value()); w.u64(r.policy.value()); w.u64(r.policyGeneration.value());
    w.u64(r.selection.value()); w.u64(r.selectionGeneration.value()); w.u64(r.evidenceGeneration.value());
    w.u64(r.worker.value()); w.u64(r.boot.value()); w.u64(r.epoch.value()); w.u64(r.bytes);
    w.u8((uint8_t)r.state); w.u64(r.createdAtMs); w.u64(r.expiresAtMs); w.str(r.note);
    return Result<std::vector<std::uint8_t>>::ok(w.data());
}
Result<Reservation> decodeReservation(const std::vector<std::uint8_t>& b) {
    Reader r(b.data(), b.size());
    Reservation rr; std::uint64_t v; std::uint8_t u; 
    if(!r.u64(v))return failCodec<Reservation>("id"); rr.id=ReservationId(v);
    if(!r.u64(v))return failCodec<Reservation>("gen"); rr.generation=ReservationGeneration(v);
    if(!r.u64(v))return failCodec<Reservation>("consumer"); rr.consumer=ConsumerId(v);
    if(!r.u64(v))return failCodec<Reservation>("cgen"); rr.consumerGeneration=ConsumerGeneration(v);
    if(!r.u64(v))return failCodec<Reservation>("provider"); rr.provider=ProviderId(v);
    if(!r.u64(v))return failCodec<Reservation>("pgen"); rr.providerGeneration=ProviderGeneration(v);
    if(!r.u64(v))return failCodec<Reservation>("region"); rr.region=RegionId(v);
    if(!r.u64(v))return failCodec<Reservation>("rgen"); rr.regionGeneration=RegionGeneration(v);
    if(!r.u64(v))return failCodec<Reservation>("pool"); rr.pool=PoolId(v);
    if(!r.u64(v))return failCodec<Reservation>("poolgen"); rr.poolGeneration=PoolGeneration(v);
    if(!r.u64(v))return failCodec<Reservation>("policy"); rr.policy=PolicyId(v);
    if(!r.u64(v))return failCodec<Reservation>("polgen"); rr.policyGeneration=PolicyGeneration(v);
    if(!r.u64(v))return failCodec<Reservation>("sel"); rr.selection=SelectionId(v);
    if(!r.u64(v))return failCodec<Reservation>("selgen"); rr.selectionGeneration=SelectionGeneration(v);
    if(!r.u64(v))return failCodec<Reservation>("evgen"); rr.evidenceGeneration=EvidenceGeneration(v);
    if(!r.u64(v))return failCodec<Reservation>("worker"); rr.worker=WorkerId(v);
    if(!r.u64(v))return failCodec<Reservation>("boot"); rr.boot=WorkerBootId(v);
    if(!r.u64(v))return failCodec<Reservation>("epoch"); rr.epoch=CoordinatorEpoch(v);
    if(!r.u64(rr.bytes))return failCodec<Reservation>("bytes");
    if(!r.u8(u))return failCodec<Reservation>("state"); rr.state=(ReservationState)u;
    if(!r.u64(rr.createdAtMs))return failCodec<Reservation>("created");
    if(!r.u64(rr.expiresAtMs))return failCodec<Reservation>("expires");
    if(!r.str(rr.note))return failCodec<Reservation>("note");
    if(!r.finish())return failCodec<Reservation>("trailing");
    return Result<Reservation>::ok(std::move(rr));
}

} // namespace memory_expansion_fabric
