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

