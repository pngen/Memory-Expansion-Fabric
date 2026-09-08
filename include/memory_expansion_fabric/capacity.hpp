#pragma once
// Memory Expansion Fabric - exact, auditable capacity accounting.
//
// Every byte in a governed region belongs to exactly one category. The ledger
// enforces, on every mutation, the closure invariant
//
//     free + reserved + committed + draining + unavailable == governed_online
//     governed_online <= total
//
// All arithmetic is checked (no overflow, no underflow). A mutation that would
// violate the invariant (or overflow) fails and leaves the ledger unchanged.
#include <cstdint>
#include <string>

namespace memory_expansion_fabric {

class CapacityLedger {
public:
    explicit CapacityLedger(std::uint64_t total_bytes)
        : total_(total_bytes) {}

    // Fundamental accessors.
    std::uint64_t total() const { return total_; }
    std::uint64_t governedOnline() const { return governed_online_; }
    std::uint64_t free() const { return free_; }
    std::uint64_t reserved() const { return reserved_; }
    std::uint64_t committed() const { return committed_; }
    std::uint64_t draining() const { return draining_; }
    std::uint64_t unavailable() const { return unavailable_; }

    bool invariant() const {
        // overflow-safe sum
        uint64_t sum = 0;
        for (uint64_t v : {free_, reserved_, committed_, draining_, unavailable_}) {
            if (v > UINT64_MAX - sum) return false;
            sum += v;
        }
        if (sum != governed_online_) return false;
        if (governed_online_ > total_) return false;
        return true;
    }

    // Set the governed online capacity. The free bucket absorbs the change;
    // this fails if online drops below the sum of non-free obligations.
    bool setOnline(std::uint64_t online_bytes) {
        if (online_bytes > total_) return false;
        uint64_t obligations = 0;
        for (uint64_t v : {reserved_, committed_, draining_, unavailable_}) {
            if (v > UINT64_MAX - obligations) return false;
            obligations += v;
        }
        if (online_bytes < obligations) return false;
        governed_online_ = online_bytes;
        free_ = online_bytes - obligations;
        return invariant();
    }

    bool reserve(std::uint64_t n) {
        if (n > free_) return false;
        free_ -= n;
        if (reserved_ > UINT64_MAX - n) { free_ += n; return false; }
        reserved_ += n;
        return invariant();
    }
    bool commit(std::uint64_t n) {
        if (n > reserved_) return false;
        reserved_ -= n;
        if (committed_ > UINT64_MAX - n) { reserved_ += n; return false; }
        committed_ += n;
        return invariant();
    }
    bool release(std::uint64_t n) {
        if (n > reserved_) return false;
        reserved_ -= n;
        if (free_ > UINT64_MAX - n) { reserved_ += n; return false; }
        free_ += n;
        return invariant();
    }
    bool complete(std::uint64_t n) {
        if (n > committed_) return false;
        committed_ -= n;
        if (free_ > UINT64_MAX - n) { committed_ += n; return false; }
        free_ += n;
        return invariant();
    }
    bool beginDrain(std::uint64_t n) {
        if (n > free_) return false;
        free_ -= n;
        if (draining_ > UINT64_MAX - n) { free_ += n; return false; }
        draining_ += n;
        return invariant();
    }
    bool endDrain(std::uint64_t n) {
        if (n > draining_) return false;
        draining_ -= n;
        if (unavailable_ > UINT64_MAX - n) { draining_ += n; return false; }
        unavailable_ += n;
        return invariant();
    }
    bool cancelDrain(std::uint64_t n) {
        if (n > draining_) return false;
        draining_ -= n;
        if (free_ > UINT64_MAX - n) { draining_ += n; return false; }
        free_ += n;
        return invariant();
    }
    bool markUnavailable(std::uint64_t n) {
        if (n > free_) return false;
        free_ -= n;
        if (unavailable_ > UINT64_MAX - n) { free_ += n; return false; }
        unavailable_ += n;
        return invariant();
    }
    bool restore(std::uint64_t n) {
        if (n > unavailable_) return false;
        unavailable_ -= n;
        if (free_ > UINT64_MAX - n) { unavailable_ += n; return false; }
        free_ += n;
        return invariant();
    }
    // Fencing moves promised capacity away from reservation/commit without
    // changing governed_online.
    bool fenceReserved(std::uint64_t n) {
        if (n > reserved_) return false;
        reserved_ -= n;
        if (unavailable_ > UINT64_MAX - n) { reserved_ += n; return false; }
        unavailable_ += n;
        return invariant();
    }
    bool fenceCommitted(std::uint64_t n) {
        if (n > committed_) return false;
        committed_ -= n;
        if (unavailable_ > UINT64_MAX - n) { committed_ += n; return false; }
        unavailable_ += n;
        return invariant();
    }

    // Balance text for diagnostics.
    std::string describe() const {
        return "total=" + std::to_string(total_) + " online=" +
               std::to_string(governed_online_) + " free=" + std::to_string(free_) +
               " reserved=" + std::to_string(reserved_) + " committed=" +
               std::to_string(committed_) + " draining=" + std::to_string(draining_) +
               " unavailable=" + std::to_string(unavailable_);
    }

private:
    std::uint64_t total_ = 0;
    std::uint64_t governed_online_ = 0;
    std::uint64_t free_ = 0;
    std::uint64_t reserved_ = 0;
    std::uint64_t committed_ = 0;
    std::uint64_t draining_ = 0;
    std::uint64_t unavailable_ = 0;
};

} // namespace memory_expansion_fabric
