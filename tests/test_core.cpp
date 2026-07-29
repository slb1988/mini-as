#include "test.hpp"
#include "mini_as/core.hpp"
#include "mini_as/engine.hpp"

TEST_CASE(core_values_and_diagnostics) {
    CHECK(mini_as::Version() == "0.1.0-learning");
    mini_as::DiagnosticSink diagnostics;
    diagnostics.Report({"test", 3, 2, 4}, mini_as::Severity::Warning, "sample");
    CHECK(!diagnostics.HasErrors());
    CHECK(diagnostics.All().size() == 1);
    CHECK(mini_as::DataType::Object("Node", true).Name() == "Node@");
    CHECK(mini_as::Value(42).Type() == mini_as::DataType::Int());
    CHECK(mini_as::Value(2.5f).ToString() == "2.5");
}

