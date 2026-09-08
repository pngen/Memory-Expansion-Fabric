# Memory Expansion Fabric

Memory Expansion Fabric is a vendor-neutral C++20 runtime for discovering,
representing, admitting, reserving, ranking, governing, degrading, draining,
revalidating, and recovering expanded or pooled memory capacity beyond a
workload's immediate local memory domain. It makes heterogeneous
expanded-memory capacity an explicit governed infrastructure resource rather
than an informal collection of "extra bytes."

It is a control plane, not an allocator. It does not move bytes, allocate GPU
memory, or implement a transfer fabric. It owns the *governing facts* about
expanded capacity.

## Core question

Which expanded-memory capacity exists now, where is it reachable from, which
consumers may safely use it, under what locality, capability, health, latency,
bandwidth, persistence, generation, and authority constraints — and when should
the system admit, prefer, avoid, drain, fail over, or revalidate that capacity?

The runtime never collapses distinct states. It distinguishes, operationally:

`capacity exists` != `capacity is online` != `capacity is reachable` !=
`capacity is compatible` != `capacity is current` != `capacity is eligible for
this consumer` != `capacity has been reserved` != `capacity has been committed` !=
`capacity may still authorize access now`.

## Systems boundary

Memory Expansion Fabric owns the generic expanded-memory control-plane boundary.
It may govern memory sourced from heterogeneous expansion mechanisms (CXL-class,
pooled host memory, remote-memory appliances, memory-expansion devices,
fabric-attached memory, accelerator-adjacent expansion, software-defined
expanded-memory tiers, and synthetic proof domains). It owns domain identity,
provider/region/pool identity, capability and health state, reachability,
compatibility, locality evidence, capacity accounting, reservations,
commitments, draining, access eligibility, admission, ranking, fallback,
policy, generation-bound authority, evidence freshness, failure-domain
information, recovery/revalidation state, persistence of durable control-plane
structure, conservative restart behavior, inspection, and explanation.

It does **not** own: malloc/free semantics, arbitrary GPU allocation, allocator
internals, GPU Memory Service, Unified Buffer ownership/lifetime, Transfer
Fabric movement, cache semantics, FlashTier paging, generic NUMA/PCIe topology,
CXL protocol, CXL device firmware, CXL-specific discovery, RDMA, GPUDirect,
NIC/DPU execution, NVLink/NVSwitch routing, storage paging, workload
scheduling, global placement, checkpointing, replica management, general
resource arbitration, page-fault machinery, transparent OS swapping, distributed
shared-memory coherence, cache coherence, or application serialization.

It is intentionally **not** a second CXL Fabric, Unified Buffer, Transfer
Fabric, GPU Memory Service, NUMA library, storage cache, remote-memory protocol,
generic allocator, or global scheduler. It does not require CXL Fabric as a
dependency; the standalone core builds and operates without any other Summon
Software Labs repository.

## Architecture

`include/memory_expansion_fabric/`   public C++20 API (namespace memory_expansion_fabric)
`src/`                               core runtime: fabric, selection, reservation,
                                       persistence, protocol, backends
`tools/cli.cpp`                      inspection CLI (mef_cli)
`tools/worker_main.cpp`              reference worker (mef_worker)
`tools/coordinator_main.cpp`         reference coordinator (mef_coordinator)
`examples/`                          runnable contract examples
`benchmarks/`                        completed-work benchmark
`proofs/`                            real CUDA consumer-gating proof (build script)
`tests/`                             unit / property / concurrency / adversarial /
                                       system / multiprocess suites
`cmake/`                             installable CMake package config

The core is a single static library `mef_core`. It links only the C++ standard
library (and, for the reference multiprocess deployment and real system
discovery, the Windows SDK). There are no third-party runtime dependencies.

## Identity / authority model

Every identity is a strongly typed 64-bit id (`TypedId<Tag>`), never an
interchangeable raw string: ProviderId, ProviderGeneration, ExpansionDomainId,
ExpansionDomainGeneration, RegionId, RegionGeneration, PoolId, PoolGeneration,
AttachmentId, AttachmentGeneration, ConsumerId, ConsumerGeneration,
ReservationId, ReservationGeneration, PolicyId, PolicyGeneration, EvidenceId,
EvidenceGeneration, WorkerId, WorkerBootId, CoordinatorId, CoordinatorEpoch,
FailureDomainId, SelectionId, SelectionGeneration. A null/zero identity is never
valid; deserialization validates, and duplicates are rejected where uniqueness
is required.

Every dynamic publication and mutating action is fenced by an `Authority`
tuple (CoordinatorEpoch + optional WorkerId/WorkerBootId). Stale epoch, stale
boot, or stale generation is rejected **before** mutation. Operator authority
(no worker) is allowed for direct inspection; worker authority requires the
worker to be registered, alive, and of the current boot id under the current
epoch.

## Capacity / accounting model

Each governed region uses an exact `CapacityLedger`. The closure invariant

`free + reserved + committed + draining + unavailable == governed_online` and
`governed_online <= total`

is enforced on every mutation with checked arithmetic (no overflow, no
underflow, no double-count). A failed operation leaves state unchanged.
Overlapping logical views (pools) never double-count physical backing because
reservations are bound to the canonical backing region: pool membership is a
view, and physical capacity authority lives in the region. A pool's aggregate
logical capacity cannot exceed the backing region's physical authority.

## Provider model

A provider carries kind (SYSTEM_MEMORY, HOST_EXPANSION, CXL_CLASS,
REMOTE_MEMORY, FABRIC_ATTACHED, ACCELERATOR_ADJACENT, SOFTWARE_DEFINED,
SYNTHETIC, UNKNOWN), origin (REAL/SYNTHETIC/UNSUPPORTED/UNKNOWN), total
capacity, persistence, addressability, alignment, granularity, failure-domain,
and an explicit capability matrix. Presenting an enum value never implies the
hardware exists.

## Lifecycle

Provider/domain lifecycle: DISCOVERED -> REGISTERED -> PROBING -> ONLINE ->
DEGRADED -> DRAINING -> OFFLINE -> REVALIDATION_REQUIRED -> FAILED -> RETIRED.
Region lifecycle: DECLARED -> ONLINE -> DEGRADED -> DRAINING -> UNAVAILABLE ->
REVALIDATION_REQUIRED -> RETIRED. Transitions are guarded; illegal transitions
are rejected and tested. A restarted coordinator never silently converts
recovered dynamic ONLINE state to authoritative ONLINE; it becomes
REVALIDATION_REQUIRED until fresh provider evidence is published.

## Eligibility

Hard eligibility is evaluated before ranking. Typed rejection reasons include:
INSUFFICIENT_CAPACITY, OFFLINE, DEGRADED_NOT_ALLOWED, UNREACHABLE,
CAPABILITY_UNSUPPORTED, CAPABILITY_UNKNOWN, INCOMPATIBLE, TOO_REMOTE,
LATENCY_LIMIT_EXCEEDED, BANDWIDTH_INSUFFICIENT, PERSISTENCE_REQUIRED,
STALE_EVIDENCE, REVALIDATION_REQUIRED, WRONG_GENERATION, WRONG_EPOCH,
POLICY_DENIED, FAILURE_DOMAIN_CONFLICT. An ineligible candidate never wins
because of a good score. UNKNOWN capability fails closed when the fact is
required for safe admission.

## Deterministic ranking

After eligibility, candidates are ranked deterministically by named, weighted,
normalized factors (locality, latency, bandwidth, headroom, access kind,
health). The result carries ranked candidates, rejected candidates with hard
reasons, ranking factors, and a deterministic tie-break rule
("score desc, then region id, provider id, pool id"). Ranking is stable under
insertion-order permutations. Selection is a plan; it does not authorize use.

## Reservation / commit lifecycle

REQUESTED -> EVALUATED -> RESERVED -> COMMITTED -> ACTIVE -> RELEASED, with failure
states REJECTED, EXPIRED, FENCED, REVALIDATION_REQUIRED, CANCELLED. A
reservation binds provider/domain/region/pool/policy/evidence generations,
consumer generation, worker boot, and coordinator epoch. Commit revalidates
every binding against current state; a stale reservation cannot commit after a
capacity shrink, provider degradation, worker death, coordinator restart, pool/
policy/consumer generation change, region replacement, or evidence expiration.

## Drain / degradation / withdrawal

`beginDrain` returns reserved authority to free and moves all free capacity to
draining (no new reservations), preserving committed authority per policy;
`completeDrain` moves draining capacity to unavailable; `cancelDrain` restores
it. `markUnavailable` fences reserved/committed authority. The runtime may
*plan* movement through a narrow boundary but never claims bytes moved unless an
actual migration backend performed the move.

## Persistence / recovery

Durable structure (provider/domain/region/pool/policy/consumer identities) is
saved to a versioned, bounded, integrity-checked (CRC-32) file written
atomically (temp + replace). Load rejects corruption, truncation, malformed
fields, huge counts, and trailing garbage. On load the coordinator advances to
the next epoch, dynamic evidence becomes REVALIDATION_REQUIRED, and
reservations are absent (non-durable). Fresh evidence and fresh authority are
required before eligibility returns.

## Process model (reference deployment)

Independent OS processes: a `mef_coordinator` hosts the Fabric and a framed
loopback TCP server; `mef_worker` processes connect, register a WorkerBootId,
and publish evidence. The coordinator spawns workers as children and owns their
process handles, which it uses to prove worker death. Frames are bounded,
versioned, type-validated against an explicit set, length-validated, and
CRC-integrity-checked; malformed/truncated/oversized/unknown-type frames are
rejected. Partial send/receive and clean EOF are handled.

## REAL / SYNTHETIC / UNSUPPORTED

The runtime never blurs categories. The system backend inspects the actual
machine: physical expansion-class memory is reported UNSUPPORTED when absent,
and host system memory is reported REAL (as host memory, not expansion
hardware). The synthetic backend's deterministic multi-provider scenarios are
labeled SYNTHETIC. The CUDA proof is REAL where a GPU is actually used.

## Build

Prerequisites: CMake >= 3.24, a C++20 compiler (MSVC 2022 recommended), and for
the CUDA proof an NVIDIA CUDA toolkit + GPU.

`cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
`cmake --build build --config Release`
`cmake --install build --config Release --prefix <prefix>`

The core library `mef_core` builds standalone with /W4 /WX (MSVC); first-party
code is warning-clean. An AddressSanitizer build is supported:

`cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DMEF_ENABLE_ASAN=ON`
`cmake --build build-asan --config Debug`

## Test

`ctest --test-dir build -C RelWithDebInfo`   (or run the executables in build/bin)

Suites cover core lifecycle, capacity/accounting, pooling, eligibility,
ranking, reservation, drain/degradation, persistence, protocol codecs,
property/invariant testing with reproducible seeds, real-thread concurrency
(no hangs), adversarial input, real Windows system discovery, the multiprocess
worker-death/reincarnation proof, and the coordinator-restart proof. The core,
property, adversarial, and concurrency suites also run under AddressSanitizer.
No test uses a timeout or a watchdog; a hanging test is treated as a defect.

## Examples

Runnable examples (in `build/bin`) demonstrate distinct implemented contracts:
provider/domain/region registration and PRESENT-neq-ONLINE; consumer requirements
and deterministic selection with ranking factors; reserve/commit/release and
capacity closure; drain/degradation/recovery; conservative persistence restart;
the synthetic multi-provider scenario; and real Windows system discovery.

## CLI

`mef_cli` is a narrow inspection tool:

`mef_cli discover  --backend system|synthetic|unsupported`
`mef_cli inspect   --backend system|synthetic|unsupported`
`mef_cli demo      --backend system|synthetic|unsupported`

Output distinguishes REAL / SYNTHETIC / UNSUPPORTED / UNKNOWN where relevant.

## Benchmark

`mef_benchmark` measures completed operations (no async submission, no omitted
finalization): ingestion, selection, reserve/commit/release, snapshot, and
persistence save/load, scaled through configurable sizes (default 100/1000/10000,
arbitrary via argv).

## Package consumption

`find_package(MemoryExpansionFabric CONFIG REQUIRED)`
`target_link_libraries(app PRIVATE MemoryExpansionFabric::mef_core)`

The install tree exports headers, `lib/mef_core.lib`, and
`lib/cmake/MemoryExpansionFabric/{Config,ConfigVersion,Targets}.cmake`. An
independent downstream consumer was built and run against a temporary install
prefix to verify `find_package`, header inclusion, linking, and a real API
operation.

## Actual hardware validation

- Real Windows system discovery executed: host physical memory is REAL; all
  physical expansion classes (CXL, remote, pooled, fabric-attached) are
  reported UNSUPPORTED because none is installed.
- Real NVIDIA CUDA: an NVIDIA GeForce RTX 5090 (compute capability 12.0) was
  used for the gating proof — real device discovery, host allocation, H2D, a
  real kernel, D2H, CPU parity, and clean-up; GPU memory returned to baseline.
- A real worker process was spawned, published evidence, and was terminated as
  an OS process; the coordinator's process handle detected the death, fenced the
  authority, and re-admitted fresh authority only after a new WorkerBootId.

## Genuine limitations

- No physical CXL, remote, pooled, or fabric-attached memory hardware is present
  on the validation machine. Memory Expansion Fabric's semantics for such
  technology are proven with SYNTHETIC domains and verified against the evidence
  model; they are not a claim that physical hardware exists or was tested.
- CUDA is used to prove *gating* of a real accelerator consumer. It is not a
  claim of physical memory expansion, and the Fabric does not allocate GPU
  memory.
- Transparent memory migration, page-fault-driven expansion, RDMA, GPUDirect,
  NVLink/NVSwitch, and hardware cache coherence are out of scope; the runtime
  never claims them.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.