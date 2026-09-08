#pragma once
// Memory Expansion Fabric - authority tuple.
//
// Every dynamic publication or mutating action is fenced by an Authority.
// A stale message (wrong epoch, wrong worker boot, wrong generation) must be
// rejected BEFORE any state mutation.
#include "memory_expansion_fabric/id.hpp"

namespace memory_expansion_fabric {

struct Authority {
    CoordinatorEpoch epoch;   // current coordinator epoch
    WorkerId worker;          // null => operator/local authority
    WorkerBootId boot;        // null when worker is null

    constexpr Authority(CoordinatorEpoch e) noexcept
        : epoch(e), worker(WorkerId()), boot(WorkerBootId()) {}

    constexpr Authority(CoordinatorEpoch e, WorkerId w, WorkerBootId b) noexcept
        : epoch(e), worker(w), boot(b) {}

    bool isOperator() const noexcept { return worker.null(); }

    auto operator<=>(const Authority&) const = default;
};

} // namespace memory_expansion_fabric
