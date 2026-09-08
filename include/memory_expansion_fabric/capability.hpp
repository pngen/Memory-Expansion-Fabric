#pragma once
// Memory Expansion Fabric - explicit capability model.
#include "memory_expansion_fabric/enums.hpp"

#include <string>
#include <vector>

namespace memory_expansion_fabric {

// A single capability fact. UNKNOWN is distinct from UNSUPPORTED and must
// never be collapsed to false; UNKNOWN fails closed whenever the fact is
// required for safe admission or use.
struct Capability {
    CapabilityKey key = CapabilityKey::CPU_ACCESSIBLE;
    CapabilityStatus status = CapabilityStatus::UNKNOWN;
    std::string note;

    bool supported() const { return status == CapabilityStatus::SUPPORTED; }
    bool unsupported() const { return status == CapabilityStatus::UNSUPPORTED; }
    bool unknown() const { return status == CapabilityStatus::UNKNOWN; }
    bool revalidationRequired() const {
        return status == CapabilityStatus::REVALIDATION_REQUIRED;
    }
};

// A capability matrix: one status per known key, with room for notes.
class CapabilityModel {
public:
    CapabilityModel() = default;

    void set(CapabilityKey key, CapabilityStatus status, std::string note = {}) {
        for (auto& c : caps_) {
            if (c.key == key) { c.status = status; c.note = std::move(note); return; }
        }
        caps_.push_back(Capability{key, status, std::move(note)});
    }

    CapabilityStatus get(CapabilityKey key) const {
        for (const auto& c : caps_) {
            if (c.key == key) return c.status;
        }
        return CapabilityStatus::UNKNOWN;
    }

    const std::vector<Capability>& all() const { return caps_; }

private:
    std::vector<Capability> caps_;
};

} // namespace memory_expansion_fabric
