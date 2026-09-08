#include "memory_expansion_fabric/fabric.hpp"
#include <iostream>
int main() {
    memory_expansion_fabric::Fabric f;
    std::cout << "version=" << memory_expansion_fabric::Fabric::version()
              << " epoch=" << f.epoch().value() << "\n";
    return 0;
}
