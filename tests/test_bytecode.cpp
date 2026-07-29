#include "test.hpp"
#include "mini_as/bytecode.hpp"

TEST_CASE(bytecode_compiler_emits_typed_operations_and_slots) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("bytecode", "float calc(int x) { float y = x; return y + 2.0; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    CHECK(!diagnostics.HasErrors());
    CHECK(module.functions.size() == 1);
    CHECK(module.functions[0].localCount == 2);
    const auto listing = mini_as::Disassemble(module.functions[0]);
    CHECK(listing.find("TO_FLOAT") != std::string::npos);
    CHECK(listing.find("ADD_F") != std::string::npos);
    CHECK(listing.find("RET") != std::string::npos);
}

TEST_CASE(bytecode_disassembly_retains_source_lines) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("lines", "int f() {\n return 42;\n}", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    CHECK(mini_as::Disassemble(module.functions[0]).find("2:9") != std::string::npos);
}

