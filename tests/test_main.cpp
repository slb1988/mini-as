#include "mini_as/engine.hpp"

#include <iostream>

int main() {
    if (mini_as::Version() != "0.1.0-learning") {
        std::cerr << "version smoke test failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}

