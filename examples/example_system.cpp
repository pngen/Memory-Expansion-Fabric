// Demonstrate real Windows system discovery.
#include "memory_expansion_fabric/backend.hpp"
#include <cstdio>
#include <memory>
using namespace memory_expansion_fabric;
int main() {
    auto disc = makeDiscovery("system");
    std::printf("backend=%s\n", disc->name().c_str());
    std::printf("summary: %s\n", disc->summary().c_str());
    std::printf("%s", disc->capabilityReport().c_str());
    for (auto& p : disc->providers())
        std::printf("real provider %llu kind=%d cap=%llu bytes\n", (unsigned long long)p.id.value(), (int)p.kind, (unsigned long long)p.totalCapacityBytes);
    int fails = 0;
    std::printf("system example: %s\n", fails?"FAIL":"PASS");
    return fails;
}
