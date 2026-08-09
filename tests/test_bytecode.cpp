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
    CHECK(listing.find("ADD_D") != std::string::npos);
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

TEST_CASE(bytecode_globals_use_stable_ids_for_load_and_store) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("globals", "int counter = 1; int next() { counter = counter + 1; return counter; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    auto globals = checker.Globals();
    globals[0].id = mini_as::GlobalId{7};
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes(), globals);
    CHECK(!diagnostics.HasErrors());
    CHECK(module.FindGlobalIndex(mini_as::GlobalId{7}).has_value());
    bool loaded = false;
    bool stored = false;
    for (const auto& instruction : module.functions[0].code) {
        if (instruction.opcode == mini_as::OpCode::LoadGlobal) {
            loaded = true;
            CHECK(instruction.operand == 7);
        }
        if (instruction.opcode == mini_as::OpCode::StoreGlobal) {
            stored = true;
            CHECK(instruction.operand == 7);
        }
    }
    CHECK(loaded);
    CHECK(stored);
}

TEST_CASE(bytecode_emits_explicit_integer_width_conversions) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("integer-conversion",
        "int64 widen(int8 value) { uint16 next = value; return next + 1; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    CHECK(!diagnostics.HasErrors());
    int conversions = 0;
    for (const auto& instruction : module.functions[0].code)
        if (instruction.opcode == mini_as::OpCode::ToInteger) ++conversions;
    CHECK(conversions >= 3);
}

TEST_CASE(bytecode_uses_double_operations_and_explicit_narrowing) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("double-bytecode",
        "float narrow(double value) { return value + 0.25; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    const auto listing = mini_as::Disassemble(module.functions[0]);
    CHECK(listing.find("ADD_D") != std::string::npos);
    CHECK(listing.find("TO_FLOAT") != std::string::npos);
}

TEST_CASE(bytecode_emits_typed_bitwise_and_shift_operations) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("bit-bytecode",
        "int bits(int value) { value ^= 3; return (~value & 0xFF) << 1 >>> 1; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    const auto listing = mini_as::Disassemble(module.functions[0]);
    CHECK(listing.find("BIT_XOR") != std::string::npos);
    CHECK(listing.find("BIT_NOT") != std::string::npos);
    CHECK(listing.find("BIT_AND") != std::string::npos);
    CHECK(listing.find("SHL") != std::string::npos);
    CHECK(listing.find("USHR") != std::string::npos);
}

TEST_CASE(bytecode_emits_integer_and_double_power_operations) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("power-bytecode",
        "double power(int exponent) { int value = 2 ** exponent; return value + 2.0 ** exponent; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    const auto listing = mini_as::Disassemble(module.functions[0]);
    CHECK(listing.find("POW_I") != std::string::npos);
    CHECK(listing.find("POW_D") != std::string::npos);
}

TEST_CASE(bytecode_embeds_enum_constants_and_converts_them_to_int) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("enum-bytecode",
        "enum Color { Red = 2, Green, Blue = Green + 2 } int value() { return Blue; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes(),
                                   checker.Globals(), checker.Enums());
    CHECK(!diagnostics.HasErrors());
    CHECK(module.functions[0].constants.size() == 1);
    CHECK(module.functions[0].constants[0].Type() == mini_as::DataType::Enum("Color"));
    const auto listing = mini_as::Disassemble(module.functions[0]);
    CHECK(listing.find("PUSH_CONST") != std::string::npos);
    CHECK(listing.find("TO_INTEGER") != std::string::npos);
}

