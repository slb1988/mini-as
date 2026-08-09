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

TEST_CASE(bytecode_registered_globals_preserve_host_stable_ids) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("host-global-bytecode",
        "int update() { hostCounter += 2; return hostCounter; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    mini_as::GlobalSignature property{"hostCounter", mini_as::DataType::Int(), false,
                                      mini_as::GlobalId{23}, true};
    checker.RegisterGlobalProperty(property);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes(),
                                   checker.Globals());
    CHECK(!diagnostics.HasErrors());
    const auto index = module.FindGlobalIndex(mini_as::GlobalId{23});
    CHECK(index.has_value());
    CHECK(module.globals[*index].signature.host);
    bool loaded = false;
    bool stored = false;
    for (const auto& instruction : module.functions[0].code) {
        if (instruction.opcode == mini_as::OpCode::LoadGlobal) {
            loaded = true;
            CHECK(instruction.operand == 23);
        }
        if (instruction.opcode == mini_as::OpCode::StoreGlobal) {
            stored = true;
            CHECK(instruction.operand == 23);
        }
    }
    CHECK(loaded);
    CHECK(stored);
}

TEST_CASE(bytecode_lowers_registered_reference_construction_to_host_factory_calls) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("host-factory-bytecode",
        "HostRef@ make() { return HostRef(42); }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    mini_as::ClassSignature hostType;
    hostType.name = "HostRef";
    hostType.id = mini_as::TypeId{17};
    hostType.host = true;
    checker.RegisterObjectType(hostType);
    mini_as::FunctionSignature factory;
    factory.name = "f";
    factory.returnType = mini_as::DataType::Object("HostRef", true);
    factory.parameters = {mini_as::DataType::Int()};
    factory.parameterNames = {"value"};
    factory.parameterModes = {mini_as::ParameterMode::Value};
    factory.host = true;
    factory.factory = true;
    factory.objectType = "HostRef";
    factory.id = mini_as::FunctionId{23};
    checker.RegisterFunction(factory);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes());
    CHECK(!diagnostics.HasErrors());
    bool hostCall = false;
    bool allocation = false;
    for (const auto& instruction : module.functions[0].code) {
        if (instruction.opcode == mini_as::OpCode::CallHost) {
            hostCall = true;
            const auto* callable = module.FindCallable(
                static_cast<std::size_t>(instruction.operand));
            CHECK(callable != nullptr);
            CHECK(callable->kind == mini_as::CallableKind::HostFunction);
            CHECK(callable->function == mini_as::FunctionId{23});
            CHECK(callable->objectType == mini_as::TypeId{17});
        }
        allocation = allocation || instruction.opcode == mini_as::OpCode::NewObject;
    }
    CHECK(hostCall);
    CHECK(!allocation);
}

TEST_CASE(bytecode_lowers_registered_object_methods_to_host_method_descriptors) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("host-method-bytecode",
        "int read(HostRef@ value) { return value.get(); }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::FunctionSignature method;
    method.name = "get";
    method.returnType = mini_as::DataType::Int();
    method.host = true;
    method.method = true;
    method.objectType = "HostRef";
    method.id = mini_as::FunctionId{31};
    method.readOnlyMethod = true;
    mini_as::ClassSignature hostType;
    hostType.name = "HostRef";
    hostType.id = mini_as::TypeId{17};
    hostType.host = true;
    hostType.methods.push_back(method);
    mini_as::TypeChecker checker(diagnostics);
    checker.RegisterObjectType(hostType);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes());
    CHECK(!diagnostics.HasErrors());
    bool hostMethod = false;
    for (const auto& instruction : module.functions[0].code) {
        if (instruction.opcode != mini_as::OpCode::CallHost) continue;
        hostMethod = true;
        const auto* callable = module.FindCallable(static_cast<std::size_t>(instruction.operand));
        CHECK(callable != nullptr);
        CHECK(callable->kind == mini_as::CallableKind::HostMethod);
        CHECK(callable->function == mini_as::FunctionId{31});
        CHECK(callable->objectType == mini_as::TypeId{17});
    }
    CHECK(hostMethod);
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

TEST_CASE(bytecode_uses_qualified_function_and_global_symbol_ids) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("namespace-bytecode",
        "namespace Math { int base = 40; int add(int x) { return base + x; } } "
        "int main() { return Math::add(2); }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    auto globals = checker.Globals();
    globals[0].id = mini_as::GlobalId{17};
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes(), globals,
                                   checker.Enums());
    CHECK(!diagnostics.HasErrors());
    CHECK(module.functions[0].signature.name == "Math::add");
    bool qualifiedGlobal = false;
    for (const auto& instruction : module.functions[0].code)
        qualifiedGlobal = qualifiedGlobal ||
            (instruction.opcode == mini_as::OpCode::LoadGlobal && instruction.operand == 17);
    CHECK(qualifiedGlobal);
    CHECK(mini_as::Disassemble(module.functions[1]).find("CALL") != std::string::npos);
}

TEST_CASE(bytecode_materializes_missing_default_arguments_at_call_sites) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("default-bytecode",
        "int add(int value, int delta = 2) { return value + delta; } int main() { return add(40); }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    CHECK(!diagnostics.HasErrors());
    CHECK(module.functions[1].constants.size() == 2);
    CHECK(module.functions[1].constants[1].As<std::int32_t>() == 2);
    CHECK(module.callables[0].parameterCount == 2);
}

TEST_CASE(bytecode_orders_named_arguments_into_parameter_slots) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("named-bytecode",
        "int combine(int first, int second = 0, int third = 0) { return first + second + third; } "
        "int main() { return combine(third: 2, first: 40); }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    CHECK(!diagnostics.HasErrors());
    CHECK(module.functions[1].constants.size() == 3);
    CHECK(module.functions[1].constants[0].As<std::int32_t>() == 40);
    CHECK(module.functions[1].constants[1].As<std::int32_t>() == 0);
    CHECK(module.functions[1].constants[2].As<std::int32_t>() == 2);
}

TEST_CASE(bytecode_emits_reference_parameter_copy_in_and_writeback) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("reference-bytecode",
        "void update(int &out result, int &in source, int &inout total) { "
        "result = source * 2; total += result; } "
        "int global = 1; int main() { int total = 2; update(global, 20, total); return total; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes(), checker.Globals());
    CHECK(!diagnostics.HasErrors());
    CHECK(module.functions[0].signature.Declaration() ==
          "void update(int &out, int &in, int &inout)");
    const auto listing = mini_as::Disassemble(module.functions[1]);
    CHECK(listing.find("CALL") != std::string::npos);
    CHECK(listing.find("STORE_GLOBAL") != std::string::npos);
    CHECK(listing.find("STORE_LOCAL") != std::string::npos);
}

TEST_CASE(bytecode_materializes_and_dereferences_returned_storage_references) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("return-reference-bytecode",
        "int value = 1; int &access() { return value; } "
        "int main() { access() = 42; return access(); }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes(), checker.Globals());
    CHECK(!diagnostics.HasErrors());
    const auto accessor = mini_as::Disassemble(module.functions[0]);
    const auto caller = mini_as::Disassemble(module.functions[1]);
    CHECK(accessor.find("MAKE_GLOBAL_REF") != std::string::npos);
    CHECK(caller.find("STORE_REF") != std::string::npos);
    CHECK(caller.find("LOAD_REF") != std::string::npos);
}

TEST_CASE(bytecode_links_script_destructors_by_stable_type_and_function_ids) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("destructor-bytecode",
        "class Resource { ~Resource() {} } int main() { Resource@ value = Resource(); return 42; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes());
    CHECK(!diagnostics.HasErrors());
    CHECK(module.destructors.size() == 1);
    const auto destructorId = module.FindDestructor(module.destructors[0].first);
    CHECK(destructorId.IsValid());
    const auto* destructor = module.FindFunction(destructorId);
    CHECK(destructor != nullptr);
    CHECK(destructor->signature.destructor);
    CHECK(destructor->signature.Declaration() == "~Resource()");
}

TEST_CASE(bytecode_builds_class_virtual_slots_and_derived_field_layouts) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("inheritance-bytecode",
        "class Base { int first = 40; int value() { return first; } } "
        "class Derived : Base { int second = 2; int value() { return first + second; } } "
        "int read(Base@ item) { return item.value(); } "
        "int main() { Derived@ item = Derived(); return read(item); }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    auto classes = checker.Classes();
    classes[0].id = mini_as::TypeId{10};
    classes[1].id = mini_as::TypeId{11};
    std::uint32_t functionId = 20;
    for (auto& type : classes) {
        for (auto& method : type.methods) {
            method.objectType = type.name;
            method.method = true;
            method.id = mini_as::FunctionId{functionId++};
        }
    }
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), classes);
    CHECK(!diagnostics.HasErrors());
    CHECK(classes[1].fields.size() == 2);
    CHECK(classes[1].fields[0].name == "first");
    CHECK(classes[1].fields[1].name == "second");
    CHECK(!module.virtualDispatch.empty());
    bool virtualCall = false;
    for (const auto& function : module.functions)
        if (function.signature.name == "read")
            for (const auto& instruction : function.code)
                virtualCall = virtualCall || instruction.opcode == mini_as::OpCode::CallVirtual;
    CHECK(virtualCall);
}

TEST_CASE(type_metadata_preserves_member_access_and_declaring_classes) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("access-metadata",
        "class Base { private int secret; protected int value; "
        "private int hidden() { return secret; } } "
        "class Derived : Base { int read() { return value; } }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    const auto& classes = checker.Classes();
    CHECK(classes[0].fields[0].objectType == "Base");
    CHECK(classes[0].fields[0].access == mini_as::MemberAccess::Private);
    CHECK(classes[1].fields[0].objectType == "Base");
    CHECK(classes[1].fields[1].access == mini_as::MemberAccess::Protected);
    CHECK(classes[0].methods[0].access == mini_as::MemberAccess::Private);
}

TEST_CASE(bytecode_emits_stable_type_ids_for_reference_casts) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("reference-cast-bytecode",
        "class Base {} class Derived : Base {} "
        "Derived@ convert(Base@ value) { return cast<Derived>(value); }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    auto classes = checker.Classes();
    classes[0].id = mini_as::TypeId{10};
    classes[1].id = mini_as::TypeId{11};
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), classes);
    CHECK(!diagnostics.HasErrors());
    bool cast = false;
    for (const auto& instruction : module.functions[0].code) {
        if (instruction.opcode != mini_as::OpCode::CastObject) continue;
        cast = true;
        CHECK(instruction.operand == 11);
    }
    CHECK(cast);
}

TEST_CASE(bytecode_lowers_operator_overloads_to_virtual_method_calls) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("operator-bytecode",
        "class Number { int opAdd(int value) { return value; } } "
        "int run(Number@ number) { return number + 42; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    auto classes = checker.Classes();
    auto functions = checker.Functions();
    classes[0].id = mini_as::TypeId{10};
    classes[0].methods[0].id = mini_as::FunctionId{20};
    functions[0].id = mini_as::FunctionId{21};
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, functions, classes);
    CHECK(!diagnostics.HasErrors());
    CHECK(tree.root->Children()[1]->Children().back()->firstChild->firstChild->operatorMethod == "opAdd");
    bool virtualCall = false;
    for (const auto& function : module.functions) {
        if (function.signature.name != "run") continue;
        for (const auto& instruction : function.code)
            virtualCall = virtualCall || instruction.opcode == mini_as::OpCode::CallVirtual;
    }
    CHECK(virtualCall);
}

TEST_CASE(bytecode_lowers_property_accessors_to_virtual_method_calls) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("property-bytecode",
        "class Box { int stored; int value { get { return stored; } set { stored = value; } } } "
        "int run(Box@ box) { box.value = 42; return box.value; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    auto classes = checker.Classes();
    auto functions = checker.Functions();
    classes[0].id = mini_as::TypeId{10};
    std::uint32_t methodId = 20;
    for (auto& method : classes[0].methods) method.id = mini_as::FunctionId{methodId++};
    functions[0].id = mini_as::FunctionId{30};
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, functions, classes);
    CHECK(!diagnostics.HasErrors());
    const auto* assignment = tree.root->Children()[1]->Children().back()->firstChild->firstChild;
    CHECK(assignment->firstChild->propertySetter == "set_value");
    std::size_t virtualCalls = 0;
    for (const auto& function : module.functions) {
        if (function.signature.name != "run") continue;
        for (const auto& instruction : function.code)
            if (instruction.opcode == mini_as::OpCode::CallVirtual) ++virtualCalls;
    }
    CHECK(virtualCalls == 2);
}

TEST_CASE(bytecode_records_try_ranges_and_catch_targets) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("try-bytecode",
        "int run(int value) { try { return 10 / value; } catch { return 42; } }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions());
    CHECK(!diagnostics.HasErrors());
    CHECK(module.functions[0].exceptionHandlers.size() == 1);
    const auto& handler = module.functions[0].exceptionHandlers[0];
    CHECK(handler.tryBegin < handler.tryEnd);
    CHECK(handler.tryEnd < handler.catchTarget);
    CHECK(handler.catchTarget < module.functions[0].code.size());
}

TEST_CASE(bytecode_lowers_generated_copy_construction_to_object_copy) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("generated-copy-bytecode",
        "class Box { int value; } int run() { Box@ source = Box(); "
        "Box@ copied = Box(source); return copied.value; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    auto classes = checker.Classes();
    classes[0].id = mini_as::TypeId{17};
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), classes);
    CHECK(!diagnostics.HasErrors());
    bool copied = false;
    for (const auto& function : module.functions) {
        if (function.signature.name != "run") continue;
        for (const auto& instruction : function.code) {
            if (instruction.opcode != mini_as::OpCode::CopyObject) continue;
            copied = true;
            CHECK(instruction.operand == 17);
        }
    }
    CHECK(copied);
}

TEST_CASE(bytecode_omits_deleted_default_operation_bodies_and_targets) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("deleted-operation-bytecode",
        "class Locked { Locked() delete; Locked(const Locked &in other) delete; "
        "Locked &opAssign(const Locked &in other) delete; } int run() { return 42; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    mini_as::BytecodeCompiler compiler(diagnostics);
    auto module = compiler.Compile(tree.root, checker.Functions(), checker.Classes());
    CHECK(!diagnostics.HasErrors());
    CHECK(module.functions.size() == 1);
    CHECK(module.functions[0].signature.name == "run");
}

