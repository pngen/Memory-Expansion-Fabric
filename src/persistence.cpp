#include "fabric_impl.hpp"
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#endif

namespace memory_expansion_fabric {

namespace {

constexpr std::uint32_t kMaxPayload = 64u * 1024u * 1024u;
constexpr std::uint32_t kMaxCount   = 8192u;
constexpr std::uint32_t kMaxString  = 1024u;
constexpr std::uint16_t kVersion    = 1u;
constexpr std::uint8_t kMagic[4]    = {'M', 'E', 'F', 'P'};

// --- CRC-32 (IEEE 802.3) -------------------------------------------------
std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// --- Bounds-checked little-endian reader -----------------------------------
class ByteReader {
public:
    ByteReader(const std::uint8_t* p, std::size_t len) : p_(p), len_(len), pos_(0) {}
    bool u8(std::uint8_t& v) { if (!have(1)) return false; v = p_[pos_++]; return true; }
    bool u16(std::uint16_t& v) {
        if (!have(2)) return false;
        v = static_cast<std::uint16_t>(p_[pos_]) |
            (static_cast<std::uint16_t>(p_[pos_ + 1]) << 8);
        pos_ += 2; return true;
    }
    bool u32(std::uint32_t& v) {
        if (!have(4)) return false;
        v = static_cast<std::uint32_t>(p_[pos_]) |
            (static_cast<std::uint32_t>(p_[pos_ + 1]) << 8) |
            (static_cast<std::uint32_t>(p_[pos_ + 2]) << 16) |
            (static_cast<std::uint32_t>(p_[pos_ + 3]) << 24);
        pos_ += 4; return true;
    }
    bool u64(std::uint64_t& v) {
        std::uint32_t lo, hi;
        if (!u32(lo) || !u32(hi)) return false;
        v = static_cast<std::uint64_t>(lo) | (static_cast<std::uint64_t>(hi) << 32);
        return true;
    }
    bool str(std::string& s) {
        std::uint32_t n;
        if (!u32(n)) return false;
        if (n > kMaxString) return false;
        if (!have(n)) return false;
        s.assign(reinterpret_cast<const char*>(p_ + pos_), n);
        pos_ += n; return true;
    }
    bool finish() const { return pos_ == len_; }
private:
    bool have(std::size_t n) const { return n <= len_ - pos_; }
    const std::uint8_t* p_;
    std::size_t len_;
    std::size_t pos_;
};

// --- Writer ----------------------------------------------------------------
class ByteWriter {
public:
    void u8(std::uint8_t v) { b_.push_back(v); }
    void u16(std::uint16_t v) { b_.push_back(v & 0xFFu); b_.push_back((v >> 8) & 0xFFu); }
    void u32(std::uint32_t v) {
        b_.push_back(v & 0xFFu); b_.push_back((v >> 8) & 0xFFu);
        b_.push_back((v >> 16) & 0xFFu); b_.push_back((v >> 24) & 0xFFu);
    }
    void u64(std::uint64_t v) { u32(static_cast<std::uint32_t>(v)); u32(static_cast<std::uint32_t>(v >> 32)); }
    void str(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        b_.insert(b_.end(), s.begin(), s.end());
    }
    const std::vector<std::uint8_t>& data() const { return b_; }
private:
    std::vector<std::uint8_t> b_;
};

void writeCaps(ByteWriter& w, const CapabilityModel& m) {
    const auto& caps = m.all();
    w.u32(static_cast<std::uint32_t>(caps.size()));
    for (const auto& c : caps) { w.u8(static_cast<std::uint8_t>(c.key)); w.u8(static_cast<std::uint8_t>(c.status)); w.str(c.note); }
}
bool readCaps(ByteReader& r, CapabilityModel& m) {
    std::uint32_t n;
    if (!r.u32(n) || n > kMaxCount) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint8_t k, st; std::string note;
        if (!r.u8(k)) return false;
        if (!r.u8(st)) return false;
        if (!r.str(note)) return false;
        m.set(static_cast<CapabilityKey>(k), static_cast<CapabilityStatus>(st), note);
    }
    return true;
}

// --- Payload codec ----------------------------------------------------------
bool serializePayload(Fabric::Impl& s, std::vector<std::uint8_t>& out, std::string& err) {
    ByteWriter w;
    w.u64(s.epoch.value());
    w.u32(static_cast<std::uint32_t>(s.providers.size()));
    for (const auto& [id, p] : s.providers) {
        w.u64(id.value()); w.u64(p.descriptor.generation.value());
        w.u8(static_cast<std::uint8_t>(p.descriptor.kind));
        w.u8(static_cast<std::uint8_t>(p.descriptor.origin));
        w.str(p.descriptor.name);
        w.u64(p.descriptor.failureDomain.value());
        w.u8(static_cast<std::uint8_t>(p.descriptor.addressability));
        w.u8(static_cast<std::uint8_t>(p.descriptor.persistence));
        w.u64(p.descriptor.totalCapacityBytes);
        w.u64(p.descriptor.alignmentBytes);
        w.u64(p.descriptor.granularityBytes);
        w.u8(p.descriptor.requiresHostCopy ? 1 : 0);
        writeCaps(w, p.descriptor.capabilities);
    }
    w.u32(static_cast<std::uint32_t>(s.domains.size()));
    for (const auto& [id, d] : s.domains) {
        w.u64(id.value()); w.u64(d.descriptor.generation.value());
        w.str(d.descriptor.name);
        w.u8(static_cast<std::uint8_t>(d.descriptor.origin));
        w.u64(d.descriptor.failureDomain.value());
    }
    w.u32(static_cast<std::uint32_t>(s.regions.size()));
    for (const auto& [id, r] : s.regions) {
        w.u64(id.value()); w.u64(r.descriptor.generation.value());
        w.u64(r.descriptor.provider.value()); w.u64(r.descriptor.domain.value());
        w.str(r.descriptor.name);
        w.u64(r.descriptor.totalCapacityBytes);
        w.u64(r.descriptor.alignmentBytes);
        w.u64(r.descriptor.granularityBytes);
        w.u8(static_cast<std::uint8_t>(r.descriptor.origin));
        w.u8(static_cast<std::uint8_t>(r.descriptor.persistence));
        writeCaps(w, r.descriptor.staticCapabilities);
    }
    w.u32(static_cast<std::uint32_t>(s.pools.size()));
    for (const auto& [id, pl] : s.pools) {
        w.u64(id.value()); w.u64(pl.descriptor.generation.value());
        w.str(pl.descriptor.name);
        w.u8(static_cast<std::uint8_t>(pl.descriptor.origin));
        w.u32(static_cast<std::uint32_t>(pl.descriptor.members.size()));
        for (const auto& m : pl.descriptor.members) { w.u64(m.region.value()); w.u64(m.generation.value()); }
    }
    w.u32(static_cast<std::uint32_t>(s.policies.size()));
    for (const auto& [id, pol] : s.policies) {
        w.u64(id.value()); w.u64(pol.descriptor.generation.value());
        w.str(pol.descriptor.name);
        w.u8(pol.descriptor.allowDegraded ? 1 : 0);
        w.u8(pol.descriptor.requireFreshEvidence ? 1 : 0);
        w.u8(pol.descriptor.allowLocalFallback ? 1 : 0);
        w.u8(pol.descriptor.expansionRequired ? 1 : 0);
        w.u32(static_cast<std::uint32_t>(pol.descriptor.rules.size()));
        for (const auto& rule : pol.descriptor.rules) { w.str(rule.key); w.str(rule.value); }
    }
    w.u32(static_cast<std::uint32_t>(s.consumers.size()));
    for (const auto& [id, c] : s.consumers) { w.u64(id.value()); w.u64(c.generation.value()); }
    if (w.data().size() > kMaxPayload) { err = "payload too large"; return false; }
    out = w.data();
    return true;
}

bool deserializePayload(const std::uint8_t* p, std::size_t len, Fabric::Impl& s, std::string& err) {
    ByteReader r(p, len);
    std::uint64_t ep; std::uint32_t n;
    if (!r.u64(ep)) { err = "bad epoch"; return false; }
    if (ep == 0) { err = "epoch zero"; return false; }
    s.epoch = CoordinatorEpoch(ep);

    if (!r.u32(n) || n > kMaxCount) { err = "provider count"; return false; }
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint64_t id, gen, fd, tot, al, gr;
        std::uint8_t kind, origin, addr, persist, copy_;
        std::string name;
        if (!r.u64(id) || !r.u64(gen) || !r.u8(kind) || !r.u8(origin) || !r.str(name) ||
            !r.u64(fd) || !r.u8(addr) || !r.u8(persist) || !r.u64(tot) || !r.u64(al) || !r.u64(gr) ||
            !r.u8(copy_)) { err = "provider field"; return false; }
        ProviderDescriptor d;
        d.id = ProviderId(id); d.generation = ProviderGeneration(gen);
        d.kind = static_cast<ProviderKind>(kind); d.origin = static_cast<OriginClass>(origin);
        d.name = name; d.failureDomain = FailureDomainId(fd);
        d.addressability = static_cast<Addressability>(addr);
        d.persistence = static_cast<Persistence>(persist);
        d.totalCapacityBytes = tot; d.alignmentBytes = al; d.granularityBytes = gr;
        d.requiresHostCopy = copy_ != 0;
        if (!readCaps(r, d.capabilities)) { err = "provider caps"; return false; }
        ProviderRecord rec; rec.descriptor = d; rec.lifecycle = Lifecycle::REVALIDATION_REQUIRED;
        s.providers[ProviderId(id)] = std::move(rec);
    }
    if (!r.u32(n) || n > kMaxCount) { err = "domain count"; return false; }
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint64_t id, gen, fd; std::uint8_t origin; std::string name;
        if (!r.u64(id) || !r.u64(gen) || !r.str(name) || !r.u8(origin) || !r.u64(fd)) { err = "domain field"; return false; }
        ExpansionDomainDescriptor d;
        d.id = ExpansionDomainId(id); d.generation = ExpansionDomainGeneration(gen);
        d.name = name; d.origin = static_cast<OriginClass>(origin); d.failureDomain = FailureDomainId(fd);
        DomainRecord rec; rec.descriptor = d; rec.lifecycle = Lifecycle::REVALIDATION_REQUIRED;
        s.domains[ExpansionDomainId(id)] = std::move(rec);
    }
    if (!r.u32(n) || n > kMaxCount) { err = "region count"; return false; }
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint64_t id, gen, prov, dom, tot, al, gr; std::uint8_t origin, persist; std::string name;
        if (!r.u64(id) || !r.u64(gen) || !r.u64(prov) || !r.u64(dom) || !r.str(name) ||
            !r.u64(tot) || !r.u64(al) || !r.u64(gr) || !r.u8(origin) || !r.u8(persist)) { err = "region field"; return false; }
        RegionDescriptor d;
        d.id = RegionId(id); d.generation = RegionGeneration(gen); d.provider = ProviderId(prov);
        d.domain = ExpansionDomainId(dom); d.name = name; d.totalCapacityBytes = tot;
        d.alignmentBytes = al; d.granularityBytes = gr; d.origin = static_cast<OriginClass>(origin);
        d.persistence = static_cast<Persistence>(persist);
        if (!readCaps(r, d.staticCapabilities)) { err = "region caps"; return false; }
        RegionRecord rec; rec.descriptor = d; rec.lifecycle = RegionLifecycle::REVALIDATION_REQUIRED;
        rec.ledger = CapacityLedger(tot);
        rec.evidenceStatus = EvidenceStatus::UNKNOWN;
        s.regions[RegionId(id)] = std::move(rec);
    }
    if (!r.u32(n) || n > kMaxCount) { err = "pool count"; return false; }
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint64_t id, gen; std::uint8_t origin; std::string name; std::uint32_t mc;
        if (!r.u64(id) || !r.u64(gen) || !r.str(name) || !r.u8(origin) || !r.u32(mc) || mc > kMaxCount) { err = "pool field"; return false; }
        PoolDescriptor d;
        d.id = PoolId(id); d.generation = PoolGeneration(gen); d.name = name; d.origin = static_cast<OriginClass>(origin);
        for (std::uint32_t j = 0; j < mc; ++j) {
            std::uint64_t rid, rgen;
            if (!r.u64(rid) || !r.u64(rgen)) { err = "pool member"; return false; }
            d.members.push_back(PoolMember{RegionId(rid), RegionGeneration(rgen)});
        }
        PoolRecord rec; rec.descriptor = d; rec.registeredAtMs = 0;
        s.pools[PoolId(id)] = std::move(rec);
    }
    if (!r.u32(n) || n > kMaxCount) { err = "policy count"; return false; }
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint64_t id, gen; std::uint8_t ad, fe, af, er; std::string name; std::uint32_t rc;
        if (!r.u64(id) || !r.u64(gen) || !r.str(name) || !r.u8(ad) || !r.u8(fe) || !r.u8(af) ||
            !r.u8(er) || !r.u32(rc) || rc > kMaxCount) { err = "policy field"; return false; }
        PolicyDescriptor d;
        d.id = PolicyId(id); d.generation = PolicyGeneration(gen); d.name = name;
        d.allowDegraded = ad != 0; d.requireFreshEvidence = fe != 0;
        d.allowLocalFallback = af != 0; d.expansionRequired = er != 0;
        for (std::uint32_t j = 0; j < rc; ++j) {
            std::string k, v;
            if (!r.str(k) || !r.str(v)) { err = "policy rule"; return false; }
            d.rules.push_back(PolicyRule{k, v});
        }
        s.policies[PolicyId(id)] = PolicyRecord{std::move(d)};
    }
    if (!r.u32(n) || n > kMaxCount) { err = "consumer count"; return false; }
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint64_t id, gen;
        if (!r.u64(id) || !r.u64(gen)) { err = "consumer field"; return false; }
        s.consumers[ConsumerId(id)] = ConsumerRecord{ConsumerGeneration(gen)};
    }
    if (!r.finish()) { err = "trailing bytes"; return false; }
    return true;
}

bool atomicWrite(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        f.flush();
        if (!f) { f.close(); std::remove(tmp.c_str()); return false; }
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::remove(tmp.c_str()); return false;
    }
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { std::remove(tmp.c_str()); return false; }
#endif
    return true;
}

bool readFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open"; return false; }
    f.seekg(0, std::ios::end);
    std::streamoff len = f.tellg();
    if (len < 0) { err = "bad size"; return false; }
    f.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(len));
    if (len > 0) f.read(reinterpret_cast<char*>(bytes.data()), len);
    if (!f && len > 0) { err = "read failed"; return false; }
    return true;
}

} // namespace

Result<void> Fabric::save(const std::string& path) const {
    std::shared_lock lk(impl_->stateMtx);
    std::vector<std::uint8_t> payload;
    std::string err;
    if (!serializePayload(*impl_, payload, err))
        return Result<void>::fail(ErrorCode::INTERNAL, "serialize failed: " + err);
    std::uint32_t crc = crc32(payload.data(), payload.size());

    ByteWriter h;
    for (auto b : kMagic) h.u8(b);
    h.u16(kVersion);
    h.u32(static_cast<std::uint32_t>(payload.size()));
    h.u32(crc);
    std::vector<std::uint8_t> file = h.data();
    file.insert(file.end(), payload.begin(), payload.end());
    if (!atomicWrite(path, file))
        return Result<void>::fail(ErrorCode::IO_ERROR, "atomic write failed");
    return Result<void>::success();
}

Result<Fabric> Fabric::load(const std::string& path) {
    std::vector<std::uint8_t> bytes;
    std::string err;
    if (!readFile(path, bytes, err))
        return Result<Fabric>::fail(ErrorCode::IO_ERROR, "read failed: " + err);
    if (bytes.size() < 14) return Result<Fabric>::fail(ErrorCode::CORRUPT, "header short");
    for (int i = 0; i < 4; ++i)
        if (bytes[i] != kMagic[i]) return Result<Fabric>::fail(ErrorCode::CORRUPT, "magic");
    std::uint16_t ver = bytes[4] | (static_cast<std::uint16_t>(bytes[5]) << 8);
    if (ver != kVersion) return Result<Fabric>::fail(ErrorCode::CORRUPT, "version");
    std::uint32_t plen = bytes[6] | (static_cast<std::uint32_t>(bytes[7]) << 8) |
                         (static_cast<std::uint32_t>(bytes[8]) << 16) |
                         (static_cast<std::uint32_t>(bytes[9]) << 24);
    std::uint32_t crc = bytes[10] | (static_cast<std::uint32_t>(bytes[11]) << 8) |
                        (static_cast<std::uint32_t>(bytes[12]) << 16) |
                        (static_cast<std::uint32_t>(bytes[13]) << 24);
    if (plen > kMaxPayload) return Result<Fabric>::fail(ErrorCode::FRAME_TOO_LARGE, "payload too large");
    if (bytes.size() != 14u + plen) {
        if (bytes.size() < 14u + plen) return Result<Fabric>::fail(ErrorCode::FRAME_TRUNCATED, "truncated");
        return Result<Fabric>::fail(ErrorCode::CORRUPT, "trailing garbage");
    }
    std::uint32_t actual = crc32(bytes.data() + 14, plen);
    if (actual != crc) return Result<Fabric>::fail(ErrorCode::CORRUPT, "checksum");

    // Construct a fresh coordinator at the *next* epoch and fill durable
    // structure. Recovered dynamic evidence never becomes current here.
    Fabric f(CoordinatorEpoch(1));
    if (!deserializePayload(bytes.data() + 14, plen, *f.impl_, err))
        return Result<Fabric>::fail(ErrorCode::CORRUPT, "payload: " + err);
    std::uint64_t persistedEpoch = f.impl_->epoch.value();
    f.impl_->epoch = CoordinatorEpoch(persistedEpoch + 1);

    // Rebuild pool-member reverse index on regions.
    for (const auto& [pid, pl] : f.impl_->pools) {
        for (const auto& m : pl.descriptor.members) {
            auto rit = f.impl_->regions.find(m.region);
            if (rit != f.impl_->regions.end())
                rit->second.poolMemberships.push_back({pid, pl.descriptor.generation});
        }
    }
    return Result<Fabric>::ok(std::move(f));
}

} // namespace memory_expansion_fabric
