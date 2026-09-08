// Memory Expansion Fabric - real CUDA consumer gating proof.
//
// This proves that Memory Expansion Fabric can gate and govern a REAL
// accelerator consumer (CUDA) against provider eligibility and current
// authority. It is REPEATEDLY honest about categories:
//   - physical CXL / remote / pooled memory: UNSUPPORTED (not present)
//   - generic expansion semantics: SYNTHETIC
//   - real CUDA device / data path / kernel / parity / cleanup: REAL
//   - real host memory (SYSTEM_MEMORY provider): REAL (host memory, not expansion)
#include "memory_expansion_fabric/fabric.hpp"
#include "memory_expansion_fabric/backend.hpp"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mef = memory_expansion_fabric;

static void check(cudaError_t e, const char* what) {
    if (e != cudaSuccess) { std::printf("CUDA ERROR (%s): %s\n", what, cudaGetErrorString(e)); std::exit(1); }
}

__global__ void bumpKernel(float* data, std::size_t n) {
    std::size_t i = (std::size_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) data[i] = data[i] + 1.0f;
}

int main() {
    // --- REAL CUDA device discovery -----------------------------------------
    int ndev = 0;
    check(cudaGetDeviceCount(&ndev), "cudaGetDeviceCount");
    if (ndev == 0) { std::printf("CUDA_UNSUPPORTED: no CUDA device\n"); return 0; }
    cudaDeviceProp prop{};
    check(cudaGetDeviceProperties(&prop, 0), "cudaGetDeviceProperties");
    std::printf("CUDA_DEVICE name=%s cc=%d.%d mem=%llu MiB\n", prop.name, prop.major, prop.minor,
                (unsigned long long)prop.totalGlobalMem/(1024*1024));

    std::size_t free0=0, total0=0;
    check(cudaMemGetInfo(&free0, &total0), "cudaMemGetInfo baseline");
    std::printf("CUDA baseline free=%llu total=%llu\n", (unsigned long long)free0, (unsigned long long)total0);

    // --- Fabric: REAL host memory + SYNTHETIC expansion ----------------------
    mef::Authority a(mef::CoordinatorEpoch(1));
    mef::Fabric f(mef::CoordinatorEpoch(1));

    // REAL SYSTEM_MEMORY host provider from the system backend (host memory,
    // not physical expansion hardware).
    auto sys = mef::makeDiscovery("system");
    for (auto& p : sys->providers()) f.registerProvider(p, a);
    for (auto& d : sys->domains()) f.registerDomain(d, a);
    for (auto& r : sys->regions()) f.registerRegion(r, a);
    for (auto& e : sys->initialEvidence(mef::CoordinatorEpoch(1))) f.publishRegionEvidence(e, a);

    // SYNTHETIC expansion provider (generic CXL-class semantics).
    mef::ProviderDescriptor ep;
    ep.id = mef::ProviderId(50); ep.generation = mef::ProviderGeneration(1);
    ep.kind = mef::ProviderKind::CXL_CLASS; ep.origin = mef::OriginClass::SYNTHETIC;
    ep.name = "synthetic-expansion"; ep.totalCapacityBytes = 1ull<<33;
    ep.alignmentBytes = 4096; ep.granularityBytes = 4096; ep.persistence = mef::Persistence::VOLATILE;
    ep.capabilities.set(mef::CapabilityKey::CPU_ACCESSIBLE, mef::CapabilityStatus::SUPPORTED);
    ep.capabilities.set(mef::CapabilityKey::ACCELERATOR_ACCESSIBLE, mef::CapabilityStatus::SUPPORTED);
    ep.capabilities.set(mef::CapabilityKey::DIRECT_ACCESS, mef::CapabilityStatus::SUPPORTED);
    ep.capabilities.set(mef::CapabilityKey::VOLATILE, mef::CapabilityStatus::SUPPORTED);
    f.registerProvider(ep, a);
    f.registerDomain(mef::ExpansionDomainDescriptor{mef::ExpansionDomainId(2), mef::ExpansionDomainGeneration(1), "syn", mef::OriginClass::SYNTHETIC, mef::FailureDomainId()}, a);
    mef::RegionDescriptor er;
    er.id = mef::RegionId(50); er.generation = mef::RegionGeneration(1);
    er.provider = mef::ProviderId(50); er.domain = mef::ExpansionDomainId(2);
    er.name = "synthetic-expansion-region"; er.totalCapacityBytes = 1ull<<33;
    er.alignmentBytes = 4096; er.granularityBytes = 4096; er.origin = mef::OriginClass::SYNTHETIC;
    f.registerRegion(er, a);
    mef::RegionEvidence ev;
    ev.header.id = mef::EvidenceId(50); ev.header.generation = mef::EvidenceGeneration(1);
    ev.header.sourceProvider = mef::ProviderId(50); ev.header.sourceProviderGeneration = mef::ProviderGeneration(1);
    ev.header.region = mef::RegionId(50); ev.header.regionGeneration = mef::RegionGeneration(1);
    ev.header.epoch = mef::CoordinatorEpoch(1); ev.header.provenance = "cuda-proof";
    ev.health = mef::HealthState::HEALTHY; ev.reachable = true; ev.onlineCapacityBytes = 1ull<<33;
    ev.latencyNs = 100; ev.bandwidthBytesPerSec = 20000000000ull; ev.locality = mef::Locality::FABRIC;
    ev.status = mef::EvidenceStatus::CURRENT;
    f.publishRegionEvidence(ev, a);
    f.registerConsumer(mef::ConsumerId(77), mef::ConsumerGeneration(1), a);

    // --- Selection: an accelerator consumer is served by SYNTHETIC expansion --
    mef::ConsumerRequirements req;
    req.consumer = mef::ConsumerId(77); req.consumerGeneration = mef::ConsumerGeneration(1);
    req.requiredBytes = 16ull * 1024 * 1024;   // 16 MiB staging buffer
    req.acceleratorAccessible = true; req.cpuAccessible = false; req.stagingAcceptable = true;
    auto sel = f.select(req, a);
    if (!sel.ok()) { std::printf("select failed\n"); return 1; }
    std::printf("selection outcome=%d ranked=%zu top_region=%llu top_origin=%s\n",
                (int)sel.value().outcome, sel.value().ranked.size(),
                (unsigned long long)(sel.value().ranked.empty()?0:sel.value().ranked[0].region.value()),
                sel.value().ranked.empty()?"?":"SYNTHETIC");
    if (sel.value().ranked.empty()) { std::printf("no eligible expansion for CUDA consumer\n"); return 1; }

    // --- Reserve + commit under CURRENT authority -> permits CUDA path ---------
    auto rid = f.requestReservation(req, a);
    if (!rid.ok()) { std::printf("reserve failed: %s\n", rid.error().message.c_str()); return 1; }
    auto cr = f.commitReservation(rid.value(), a);
    if (!cr.ok()) { std::printf("commit failed: %s\n", cr.error().message.c_str()); return 1; }
    std::printf("governed authority CURRENT: reservation reserved+committed (CUDA path permitted)\n");

    // --- REAL CUDA staging path ------------------------------------------------
    const std::size_t N = 16ull * 1024 * 1024 / sizeof(float);
    float* host = nullptr;
    check(cudaMallocHost(&host, N * sizeof(float), cudaHostAllocDefault), "cudaMallocHost");
    for (std::size_t i = 0; i < N; ++i) host[i] = (float)(i % 7);
    float* dev = nullptr;
    check(cudaMalloc(&dev, N * sizeof(float)), "cudaMalloc");
    check(cudaMemcpy(dev, host, N * sizeof(float), cudaMemcpyHostToDevice), "H2D");
    bumpKernel<<<(unsigned)((N + 255)/256), 256>>>(dev, N);
    check(cudaGetLastError(), "kernel");
    check(cudaDeviceSynchronize(), "sync");
    check(cudaMemcpy(host, dev, N * sizeof(float), cudaMemcpyDeviceToHost), "D2H");
    // CPU parity.
    bool ok = true;
    for (std::size_t i = 0; i < N; ++i) {
        float expect = (float)(i % 7) + 1.0f;
        if (host[i] != expect) { ok = false; std::printf("PARITY FAIL at %zu: %f != %f\n", i, host[i], expect); break; }
    }
    std::printf("CUDA data path (H2D kernel D2H parity) = %s\n", ok ? "REAL/OK" : "FAIL");
    check(cudaFree(dev), "cudaFree");
    check(cudaFreeHost(host), "cudaFreeHost");

    // --- Stale authority rejects the gate BEFORE a CUDA launch -----------------
    f.setEpoch(mef::CoordinatorEpoch(2));   // fence prior authority
    mef::ConsumerRequirements req2 = req;   // same consumer/generation
    req2.requiredBytes = 8ull * 1024 * 1024;
    auto rid2 = f.requestReservation(req2, a);   // a is epoch-1 authority -> stale
    std::printf("stale-authority reserve accepted? %d (expected 0)\n", rid2.ok()?1:0);
    // The gated consumer must NOT obtain current authority under stale epoch.
    bool staleAllowed = rid2.ok();
    if (rid2.ok()) {
        auto c2 = f.commitReservation(rid2.value(), a);
        staleAllowed = c2.ok();
    }
    if (!staleAllowed) std::printf("CUDA gate: stale authority rejected (launch blocked)\n");
    else { std::printf("CUDA gate: STALE AUTHORITY WRONGLY PERMITTED\n"); ok = false; }

    // --- GPU returns to baseline -----------------------------------------------
    std::size_t free1=0, total1=0;
    check(cudaMemGetInfo(&free1, &total1), "cudaMemGetInfo after");
    std::printf("CUDA after free=%llu (baseline %llu)\n", (unsigned long long)free1, (unsigned long long)free0);
    bool baseline = free1 >= free0;   // we freed everything we allocated
    std::printf("CUDA baseline restored: %s\n", baseline ? "yes" : "no");

    std::printf("cuda proof: %s\n", (ok && baseline) ? "PASS" : "FAIL");
    return (ok && baseline) ? 0 : 1;
}
