#pragma once
// Memory Expansion Fabric - strongly typed identities.
#include <cstdint>
#include <functional>
#include <compare>
#include <atomic>

namespace memory_expansion_fabric {

// A strongly typed 64-bit identifier. Never interchangeable with raw integers
// or with identifiers of a different kind. A null (zero) id is never valid.
template <typename Tag>
class TypedId {
public:
    constexpr TypedId() noexcept = default;
    explicit constexpr TypedId(std::uint64_t value) noexcept : value_(value) {}

    constexpr std::uint64_t value() const noexcept { return value_; }
    constexpr bool null() const noexcept { return value_ == 0; }
    constexpr explicit operator bool() const noexcept { return value_ != 0; }

    constexpr auto operator<=>(const TypedId&) const = default;

private:
    std::uint64_t value_ = 0;
};

// --- Identity kind tags -------------------------------------------------
// Each distinct identity kind gets its own id and generation tag so that the
// compiler never allows one identity to be passed where another is required.
struct ProviderIdTag {};
struct ProviderGenTag {};
struct DomainIdTag {};
struct DomainGenTag {};
struct RegionIdTag {};
struct RegionGenTag {};
struct PoolIdTag {};
struct PoolGenTag {};
struct AttachmentIdTag {};
struct AttachmentGenTag {};
struct ConsumerIdTag {};
struct ConsumerGenTag {};
struct ReservationIdTag {};
struct ReservationGenTag {};
struct PolicyIdTag {};
struct PolicyGenTag {};
struct EvidenceIdTag {};
struct EvidenceGenTag {};
struct WorkerIdTag {};
struct WorkerBootTag {};
struct CoordinatorIdTag {};
struct CoordinatorEpochTag {};
struct FailureDomainIdTag {};
struct SelectionIdTag {};
struct SelectionGenTag {};

// --- Strongly typed aliases ---------------------------------------------
using ProviderId          = TypedId<ProviderIdTag>;
using ProviderGeneration  = TypedId<ProviderGenTag>;
using ExpansionDomainId   = TypedId<DomainIdTag>;
using ExpansionDomainGeneration = TypedId<DomainGenTag>;
using RegionId            = TypedId<RegionIdTag>;
using RegionGeneration    = TypedId<RegionGenTag>;
using PoolId              = TypedId<PoolIdTag>;
using PoolGeneration      = TypedId<PoolGenTag>;
using AttachmentId        = TypedId<AttachmentIdTag>;
using AttachmentGeneration = TypedId<AttachmentGenTag>;
using ConsumerId          = TypedId<ConsumerIdTag>;
using ConsumerGeneration  = TypedId<ConsumerGenTag>;
using ReservationId       = TypedId<ReservationIdTag>;
using ReservationGeneration = TypedId<ReservationGenTag>;
using PolicyId            = TypedId<PolicyIdTag>;
using PolicyGeneration    = TypedId<PolicyGenTag>;
using EvidenceId          = TypedId<EvidenceIdTag>;
using EvidenceGeneration  = TypedId<EvidenceGenTag>;
using WorkerId            = TypedId<WorkerIdTag>;
using WorkerBootId        = TypedId<WorkerBootTag>;
using CoordinatorId       = TypedId<CoordinatorIdTag>;
using CoordinatorEpoch    = TypedId<CoordinatorEpochTag>;
using FailureDomainId     = TypedId<FailureDomainIdTag>;
using SelectionId         = TypedId<SelectionIdTag>;
using SelectionGeneration = TypedId<SelectionGenTag>;

constexpr std::uint64_t InvalidValue = 0;
constexpr std::uint64_t FirstValidValue = 1;

// Monotonic id allocator used within the runtime. Thread-safe.
class IdAllocator {
public:
    IdAllocator() = default;

    template <typename Id>
    Id next() {
        // Roll over is deliberately prohibited: we never reuse/rebirth an id.
        return Id(nextValue());
    }

private:
    std::uint64_t nextValue() {
        // fetch-add starting at FirstValidValue; never yields zero.
        std::uint64_t v = next_.fetch_add(1);
        if (v == 0) return nextValue();  // skip zero
        return v;
    }
    std::atomic<std::uint64_t> next_{FirstValidValue};
};

} // namespace memory_expansion_fabric

namespace std {
template <typename Tag>
struct hash<memory_expansion_fabric::TypedId<Tag>> {
    size_t operator()(const memory_expansion_fabric::TypedId<Tag>& id) const noexcept {
        return std::hash<std::uint64_t>()(id.value());
    }
};
} // namespace std
