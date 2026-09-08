#include "memory_expansion_fabric/backend.hpp"
#include <cstdint>
#include <string>

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#endif

namespace memory_expansion_fabric {

namespace {

std::uint64_t totalPhysicalRam() {
#ifdef _WIN32
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    if (::GlobalMemoryStatusEx(&ms)) return ms.ullTotalPhys;
#endif
    return 0;
}

std::uint64_t processorCount() {
#ifdef _WIN32
    SYSTEM_INFO si{}; ::GetSystemInfo(&si); return si.dwNumberOfProcessors;
#endif
    return 0;
}

} // namespace

// ---- System backend ---------------------------------------------------------
class SystemDiscovery : public Discovery {
public:
    std::string name() const override { return "system"; }
    OriginClass originClass() const override { return OriginClass::REAL; }
    std::string summary() const override {
        std::string s = "system backend: physical host memory present; "
                        "no physical memory-expansion hardware (CXL-class/remote/pooled/"
                        "fabric-attached) is installed on this machine.";
        return s;
    }
    std::string capabilityReport() const override {
        return "CXL-class expansion: UNSUPPORTED\n"
               "remote/pooled memory: UNSUPPORTED\n"
               "fabric-attached memory: UNSUPPORTED\n"
               "accelerator-adjacent expansion: UNSUPPORTED\n"
               "host SYSTEM_MEMORY: REAL\n";
    }
    std::vector<ProviderDescriptor> providers() const override {
        std::vector<ProviderDescriptor> out;
        ProviderDescriptor d;
        d.id = ProviderId(1); d.generation = ProviderGeneration(1);
        d.kind = ProviderKind::SYSTEM_MEMORY; d.origin = OriginClass::REAL;
        d.name = "host-physical-memory";
        d.totalCapacityBytes = totalPhysicalRam();
        d.alignmentBytes = 4096; d.granularityBytes = 4096;
        d.persistence = Persistence::VOLATILE;
        d.capabilities.set(CapabilityKey::CPU_ACCESSIBLE, CapabilityStatus::SUPPORTED);
        d.capabilities.set(CapabilityKey::ACCELERATOR_ACCESSIBLE, CapabilityStatus::UNKNOWN);
        d.capabilities.set(CapabilityKey::BYTE_ADDRESSABLE, CapabilityStatus::SUPPORTED);
        d.capabilities.set(CapabilityKey::VOLATILE, CapabilityStatus::SUPPORTED);
        d.capabilities.set(CapabilityKey::DIRECT_ACCESS, CapabilityStatus::SUPPORTED);
        if (totalPhysicalRam() > 0) out.push_back(d);
        return out;
    }
    std::vector<ExpansionDomainDescriptor> domains() const override {
        if (totalPhysicalRam() == 0) return {};
        return { ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1),
                 "host-system-memory", OriginClass::REAL, FailureDomainId()} };
    }
    std::vector<RegionDescriptor> regions() const override {
        if (totalPhysicalRam() == 0) return {};
        std::vector<RegionDescriptor> out;
        RegionDescriptor r;
        r.id = RegionId(1); r.generation = RegionGeneration(1);
        r.provider = ProviderId(1); r.domain = ExpansionDomainId(1);
        r.name = "host-system-memory-region";
        r.totalCapacityBytes = totalPhysicalRam();
        r.alignmentBytes = 4096; r.granularityBytes = 4096;
        r.origin = OriginClass::REAL; r.persistence = Persistence::VOLATILE;
        r.staticCapabilities.set(CapabilityKey::CPU_ACCESSIBLE, CapabilityStatus::SUPPORTED);
        r.staticCapabilities.set(CapabilityKey::ACCELERATOR_ACCESSIBLE, CapabilityStatus::UNKNOWN);
        out.push_back(r);
        return out;
    }
    std::vector<RegionEvidence> initialEvidence(CoordinatorEpoch epoch) const override {
        if (totalPhysicalRam() == 0) return {};
        std::vector<RegionEvidence> out;
        RegionEvidence e;
        e.header.id = EvidenceId(1); e.header.generation = EvidenceGeneration(1);
        e.header.sourceProvider = ProviderId(1); e.header.sourceProviderGeneration = ProviderGeneration(1);
        e.header.region = RegionId(1); e.header.regionGeneration = RegionGeneration(1);
        e.header.worker = WorkerId(); e.header.boot = WorkerBootId(); e.header.epoch = epoch;
        e.header.provenance = "system-api GlobalMemoryStatusEx";
        e.health = HealthState::HEALTHY; e.reachable = true;
        e.onlineCapacityBytes = totalPhysicalRam();
        e.latencyNs = 100; e.bandwidthBytesPerSec = 30000000000ull;  // honest estimate
        e.locality = Locality::LOCAL; e.nodeId = 1;
        e.status = EvidenceStatus::CURRENT;
        out.push_back(e);
        return out;
    }
};

// ---- Synthetic backend ------------------------------------------------------
class SyntheticDiscovery : public Discovery {
public:
    std::string name() const override { return "synthetic"; }
    OriginClass originClass() const override { return OriginClass::SYNTHETIC; }
    std::string summary() const override {
        return "synthetic backend: deterministic proof-only expansion scenario; "
               "NOT real hardware.";
    }
    std::string capabilityReport() const override {
        return "CXL-class expansion: SYNTHETIC\n"
               "remote/pooled memory: SYNTHETIC\n"
               "fabric-attached memory: SYNTHETIC\n"
               "accelerator-adjacent expansion: SYNTHETIC\n"
               "generic expansion semantics: SYNTHETIC\n"
               "physical hardware: NOT PRESENT\n";
    }
    std::vector<ProviderDescriptor> providers() const override {
        std::vector<ProviderDescriptor> out;
        ProviderDescriptor cxl;
        cxl.id = ProviderId(1); cxl.generation = ProviderGeneration(1);
        cxl.kind = ProviderKind::CXL_CLASS; cxl.origin = OriginClass::SYNTHETIC;
        cxl.name = "synthetic-cxl-class";
        cxl.totalCapacityBytes = 32ull * 1024 * 1024 * 1024;
        cxl.alignmentBytes = 4096; cxl.granularityBytes = 4096;
        cxl.persistence = Persistence::VOLATILE;
        cxl.capabilities.set(CapabilityKey::CPU_ACCESSIBLE, CapabilityStatus::SUPPORTED);
        cxl.capabilities.set(CapabilityKey::ACCELERATOR_ACCESSIBLE, CapabilityStatus::SUPPORTED);
        cxl.capabilities.set(CapabilityKey::BYTE_ADDRESSABLE, CapabilityStatus::SUPPORTED);
        cxl.capabilities.set(CapabilityKey::DIRECT_ACCESS, CapabilityStatus::SUPPORTED);
        cxl.capabilities.set(CapabilityKey::VOLATILE, CapabilityStatus::SUPPORTED);
        out.push_back(cxl);

        ProviderDescriptor remote;
        remote.id = ProviderId(2); remote.generation = ProviderGeneration(1);
        remote.kind = ProviderKind::REMOTE_MEMORY; remote.origin = OriginClass::SYNTHETIC;
        remote.name = "synthetic-remote-memory";
        remote.totalCapacityBytes = 16ull * 1024 * 1024 * 1024;
        remote.alignmentBytes = 4096; remote.granularityBytes = 4096;
        remote.persistence = Persistence::PERSISTENT;
        remote.capabilities.set(CapabilityKey::CPU_ACCESSIBLE, CapabilityStatus::SUPPORTED);
        remote.capabilities.set(CapabilityKey::ACCELERATOR_ACCESSIBLE, CapabilityStatus::SUPPORTED);
        remote.capabilities.set(CapabilityKey::BYTE_ADDRESSABLE, CapabilityStatus::SUPPORTED);
        remote.capabilities.set(CapabilityKey::DIRECT_ACCESS, CapabilityStatus::SUPPORTED);
        remote.capabilities.set(CapabilityKey::PERSISTENT, CapabilityStatus::SUPPORTED);
        out.push_back(remote);
        return out;
    }
    std::vector<ExpansionDomainDescriptor> domains() const override {
        return { ExpansionDomainDescriptor{ExpansionDomainId(1), ExpansionDomainGeneration(1),
                 "synthetic-domain", OriginClass::SYNTHETIC, FailureDomainId()} };
    }
    std::vector<RegionDescriptor> regions() const override {
        std::vector<RegionDescriptor> out;
        RegionDescriptor r1;
        r1.id = RegionId(1); r1.generation = RegionGeneration(1);
        r1.provider = ProviderId(1); r1.domain = ExpansionDomainId(1);
        r1.name = "synthetic-cxl-region"; r1.totalCapacityBytes = 32ull * 1024 * 1024 * 1024;
        r1.alignmentBytes = 4096; r1.granularityBytes = 4096;
        r1.origin = OriginClass::SYNTHETIC; r1.persistence = Persistence::VOLATILE;
        out.push_back(r1);
        RegionDescriptor r2;
        r2.id = RegionId(2); r2.generation = RegionGeneration(1);
        r2.provider = ProviderId(2); r2.domain = ExpansionDomainId(1);
        r2.name = "synthetic-remote-region"; r2.totalCapacityBytes = 16ull * 1024 * 1024 * 1024;
        r2.alignmentBytes = 4096; r2.granularityBytes = 4096;
        r2.origin = OriginClass::SYNTHETIC; r2.persistence = Persistence::PERSISTENT;
        out.push_back(r2);
        return out;
    }
    std::vector<RegionEvidence> initialEvidence(CoordinatorEpoch epoch) const override {
        std::vector<RegionEvidence> out;
        RegionEvidence e1;
        e1.header.id = EvidenceId(1); e1.header.generation = EvidenceGeneration(1);
        e1.header.sourceProvider = ProviderId(1); e1.header.sourceProviderGeneration = ProviderGeneration(1);
        e1.header.region = RegionId(1); e1.header.regionGeneration = RegionGeneration(1);
        e1.header.epoch = epoch; e1.header.provenance = "synthetic";
        e1.health = HealthState::HEALTHY; e1.reachable = true;
        e1.onlineCapacityBytes = 32ull * 1024 * 1024 * 1024;
        e1.latencyNs = 120; e1.bandwidthBytesPerSec = 20000000000ull;
        e1.locality = Locality::FABRIC; e1.nodeId = 2;
        e1.status = EvidenceStatus::CURRENT;
        out.push_back(e1);
        RegionEvidence e2;
        e2.header.id = EvidenceId(2); e2.header.generation = EvidenceGeneration(1);
        e2.header.sourceProvider = ProviderId(2); e2.header.sourceProviderGeneration = ProviderGeneration(1);
        e2.header.region = RegionId(2); e2.header.regionGeneration = RegionGeneration(1);
        e2.header.epoch = epoch; e2.header.provenance = "synthetic";
        e2.health = HealthState::HEALTHY; e2.reachable = true;
        e2.onlineCapacityBytes = 16ull * 1024 * 1024 * 1024;
        e2.latencyNs = 4000; e2.bandwidthBytesPerSec = 3000000000ull;
        e2.locality = Locality::REMOTE; e2.nodeId = 3;
        e2.status = EvidenceStatus::CURRENT;
        out.push_back(e2);
        return out;
    }
};

// ---- Unsupported backend -----------------------------------------------------
class UnsupportedDiscovery : public Discovery {
public:
    std::string name() const override { return "unsupported"; }
    OriginClass originClass() const override { return OriginClass::UNSUPPORTED; }
    std::string summary() const override {
        return "unsupported backend: no memory-expansion technology is available here.";
    }
    std::string capabilityReport() const override {
        return "all physical memory-expansion classes: UNSUPPORTED\n";
    }
    std::vector<ProviderDescriptor> providers() const override { return {}; }
    std::vector<ExpansionDomainDescriptor> domains() const override { return {}; }
    std::vector<RegionDescriptor> regions() const override { return {}; }
};

std::unique_ptr<Discovery> makeDiscovery(const std::string& kind) {
    if (kind == "system") return std::make_unique<SystemDiscovery>();
    if (kind == "synthetic") return std::make_unique<SyntheticDiscovery>();
    return std::make_unique<UnsupportedDiscovery>();
}

} // namespace memory_expansion_fabric
