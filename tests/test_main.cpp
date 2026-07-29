#include "mini_as/engine.hpp"
#include "mini_as/core.hpp"

#include <iostream>

int main() {
    if (mini_as::Version() != "0.1.0-learning") {
        std::cerr << "version smoke test failed\n";
        return 1;
    }
    mini_as::DiagnosticSink diagnostics;
    diagnostics.Report({"test", 3, 2, 4}, mini_as::Severity::Warning, "sample");
    if (diagnostics.HasErrors() || diagnostics.All().size() != 1) return 2;
    if (mini_as::DataType::Object("Node", true).Name() != "Node@") return 3;
    if (mini_as::Value(42).Type() != mini_as::DataType::Int()) return 4;
    if (mini_as::Value(2.5f).ToString() != "2.5") return 5;
    std::cout << "all tests passed\n";
    return 0;
}
