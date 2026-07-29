#include "test.hpp"
#include "mini_as/interpreter.hpp"

TEST_CASE(m0_tree_pipeline_calls_host) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::TreeInterpreter interpreter(diagnostics);
    std::string printed;
    interpreter.RegisterFunction("Print", [&](const std::vector<mini_as::Value>& arguments) {
        printed = arguments.at(0).ToString(); return mini_as::Value{};
    });
    CHECK(mini_as::RunTreeScript("Print(6 * 7);", interpreter, diagnostics));
    CHECK(printed == "42");
}

