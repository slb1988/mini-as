#include "test.hpp"
#include "mini_as/vm.hpp"

namespace {
mini_as::BytecodeFunction CompileFunction(std::string_view source, mini_as::DiagnosticSink& diagnostics) {
    mini_as::Tokenizer tokenizer("vm", source, diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    if (!checker.Check(tree.root)) throw std::runtime_error("type check failed");
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    if (diagnostics.HasErrors()) throw std::runtime_error("compile failed");
    return std::move(module.functions[0]);
}
}

TEST_CASE(vm_executes_typed_stack_bytecode) {
    mini_as::DiagnosticSink diagnostics;
    auto function = CompileFunction("float calc(int x) { float y = x; return y * 2.5 + 1; }", diagnostics);
    mini_as::VirtualMachine vm;
    auto result = vm.Execute(function, {mini_as::Value(4)});
    CHECK(result.state == mini_as::ExecutionState::Finished);
    CHECK(result.returnValue.As<float>() == 11.0f);
}

TEST_CASE(vm_reports_division_by_zero_at_source) {
    mini_as::DiagnosticSink diagnostics;
    auto function = CompileFunction("int fail() {\n return 1 / 0;\n}", diagnostics);
    mini_as::VirtualMachine vm;
    auto result = vm.Execute(function);
    CHECK(result.state == mini_as::ExecutionState::Exception);
    CHECK(result.exception == "division by zero");
    CHECK(result.location.row == 2);
}

TEST_CASE(vm_executes_control_flow_and_short_circuit) {
    mini_as::DiagnosticSink diagnostics;
    auto function = CompileFunction(
        "int sum(int n) { int i = 0; int total = 0; while (i < n) { i = i + 1; "
        "if (i > 2) total = total + i; } if (false && (1 / 0 > 0)) total = 0; return total; }",
        diagnostics);
    mini_as::VirtualMachine vm;
    auto result = vm.Execute(function, {mini_as::Value(5)});
    CHECK(result.state == mini_as::ExecutionState::Finished);
    CHECK(result.returnValue.As<std::int32_t>() == 12);
}

TEST_CASE(vm_suspends_at_line_cue_and_resumes_same_stack) {
    mini_as::DiagnosticSink diagnostics;
    auto function = CompileFunction("int f() { int x = 1; x = x + 1; return x; }", diagnostics);
    mini_as::VirtualMachine vm;
    CHECK(vm.Prepare(function));
    vm.RequestSuspend();
    auto suspended = vm.Continue();
    CHECK(suspended.state == mini_as::ExecutionState::Suspended);
    auto finished = vm.Continue();
    CHECK(finished.state == mini_as::ExecutionState::Finished);
    CHECK(finished.returnValue.As<std::int32_t>() == 2);
}

