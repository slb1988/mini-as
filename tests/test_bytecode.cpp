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

TEST_CASE(bytecode_calls_reference_stable_function_ids) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("ids", "int callee() { return 7; } int caller() { return callee(); }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    CHECK(!diagnostics.HasErrors());
    const auto* callee = module.FindFunction(module.functions[0].signature.id);
    CHECK(callee != nullptr);
    const auto& caller = module.functions[1];
    bool foundCall = false;
    for (const auto& instruction : caller.code) {
        if (instruction.opcode == mini_as::OpCode::Call) {
            foundCall = true;
            const auto* callable = module.FindCallable(static_cast<std::size_t>(instruction.operand));
            CHECK(callable != nullptr);
            CHECK(callable->kind == mini_as::CallableKind::ScriptFunction);
            CHECK(callable->function == callee->signature.id);
        }
    }
    CHECK(foundCall);
}

TEST_CASE(bytecode_lvalues_cover_local_and_field_storage) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("lvalues",
        "class Box { int value; } int set(Box@ box) { int local = 1; local = 2; box.value = local; return box.value; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes());
    CHECK(!diagnostics.HasErrors());
    bool storedLocal = false;
    bool storedField = false;
    bool loadedField = false;
    for (const auto& instruction : module.functions[0].code) {
        storedLocal = storedLocal || instruction.opcode == mini_as::OpCode::StoreLocal;
        storedField = storedField || instruction.opcode == mini_as::OpCode::StoreField;
        loadedField = loadedField || instruction.opcode == mini_as::OpCode::LoadField;
    }
    CHECK(storedLocal);
    CHECK(storedField);
    CHECK(loadedField);
}

