#include "test.hpp"

#include <iostream>

std::vector<TestCase>& TestRegistry() {
    static std::vector<TestCase> tests;
    return tests;
}

TestRegistration::TestRegistration(const char* name, std::function<void()> run) {
    TestRegistry().push_back({name, std::move(run)});
}

int main() {
    int failed = 0;
    for (const auto& test : TestRegistry()) {
        try { test.run(); }
        catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }
    if (failed) return 1;
    std::cout << TestRegistry().size() << " tests passed\n";
    return 0;
}

