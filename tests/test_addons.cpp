#include "test.hpp"
#include "mini_as/addons/any.hpp"
#include "mini_as/addons/array.hpp"
#include "mini_as/addons/dictionary.hpp"
#include "mini_as/addons/ref.hpp"

#include <sstream>

namespace {

class RefPayload final : public mini_as::RefObject {
public:
    RefPayload(const mini_as::TypeInfo* type, int value) : RefObject(type), value(value) {}
    int value = 0;

private:
    ~RefPayload() override = default;
};

} // namespace

TEST_CASE(array_addon_constructs_resizes_and_stores_typed_values) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    auto* module = engine->GetModule("array-addon");
    module->AddScriptSection("array-addon",
        "int run() { array<int>@ values = array<int>(2); "
        "values.set(0, 20); values.set(1, 1); values.insertLast(21); "
        "int tail = values.removeLast(); values.resize(3); values.set(2, 1); "
        "return values.get(0) + values.get(1) + values.get(2) + tail - 1; }");
    CHECK(module->Build());
    const auto* type = engine->GetTypeInfo("array<int>");
    CHECK(type != nullptr);
    CHECK(type->collector != nullptr);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(engine->GetTrackedObjectCount() == 0);

    std::stringstream bytecode(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveBytecode(bytecode));
    auto restoredEngine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> restoreDiagnostics;
    restoredEngine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        restoreDiagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*restoredEngine));
    auto* restored = restoredEngine->GetModule("array-restored");
    bytecode.seekg(0);
    if (!restored->LoadBytecode(bytecode)) {
        std::string message;
        for (const auto& diagnostic : restoreDiagnostics)
            message += (message.empty() ? std::string{} : " | ") + diagnostic.message;
        throw std::runtime_error(message.empty()
            ? "array bytecode restore failed without a diagnostic" : message);
    }
    auto restoredContext = restoredEngine->CreateContext();
    CHECK(restoredContext->Prepare(restored->GetFunctionByDecl("int run()")));
    CHECK(restoredContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(restoredContext->GetReturnInt() == 42);
}

TEST_CASE(array_addon_reports_bounds_errors_at_the_method_call) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    auto* module = engine->GetModule("array-bounds");
    module->AddScriptSection("array-bounds",
        "int run() {\n"
        "  array<int>@ values = array<int>();\n"
        "  return values.get(0);\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("array index is out of range") !=
          std::string::npos);
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(array_addon_rejects_unsupported_reference_value_subtypes) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    CHECK(engine->RegisterObjectType("HostRef") != nullptr);
    auto* module = engine->GetModule("invalid-array-subtype");
    module->AddScriptSection("invalid-array-subtype",
        "int before() { return 0; }\narray<HostRef>@ invalid;\n");
    CHECK(!module->Build());
    bool rejected = false;
    for (const auto& diagnostic : diagnostics)
        rejected = rejected ||
            (diagnostic.location.row == 2 &&
             diagnostic.message.find("reference object array subtypes must use handles") !=
                 std::string::npos);
    CHECK(rejected);
}

TEST_CASE(array_addon_participates_in_script_object_cycle_collection) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    auto* module = engine->GetModule("array-cycle");
    module->AddScriptSection("array-cycle",
        "class Node { array<Node@>@ children = array<Node@>(); } "
        "int run() { Node@ root = Node(); root.children.insertLast(root); "
        "@root = null; return 42; }");
    if (!module->Build()) {
        std::string message;
        for (const auto& diagnostic : diagnostics)
            message += (message.empty() ? std::string{} : " | ") + diagnostic.message;
        throw std::runtime_error(message);
    }
    {
        auto context = engine->CreateContext();
        CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
        CHECK(context->Execute() == mini_as::ExecutionState::Finished);
        CHECK(context->GetReturnInt() == 42);
    }
    CHECK(engine->GetTrackedObjectCount() == 2);
    CHECK(engine->CollectGarbage() == 2);
    CHECK(engine->GetTrackedObjectCount() == 0);
}

TEST_CASE(initialization_lists_use_registered_factory_and_insert_protocol) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    auto* module = engine->GetModule("array-init-list");
    module->AddScriptSection("array-init-list",
        "int run() { array<int>@ values = {20, 21, 1}; "
        "array<int>@ empty = {}; return values.get(0) + values.get(1) + "
        "values.get(2) + int(empty.length()); }");
    if (!module->Build()) {
        std::string message;
        for (const auto& diagnostic : diagnostics)
            message += (message.empty() ? std::string{} : " | ") + diagnostic.message;
        throw std::runtime_error(message);
    }
    const auto* function = module->GetFunctionByDecl("int run()");
    CHECK(function != nullptr);
    std::size_t hostCalls = 0;
    for (const auto& instruction : function->code)
        if (instruction.opcode == mini_as::OpCode::CallHost) ++hostCalls;
    CHECK(hostCalls >= 9);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(function));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);

    std::stringstream archive(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveBytecode(archive));
    auto loadedEngine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptArray(*loadedEngine));
    auto* loaded = loadedEngine->GetModule("array-init-list-loaded");
    archive.seekg(0);
    CHECK(loaded->LoadBytecode(archive));
    auto loadedContext = loadedEngine->CreateContext();
    CHECK(loadedContext->Prepare(loaded->GetFunctionByDecl("int run()")));
    CHECK(loadedContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(loadedContext->GetReturnInt() == 42);
}

TEST_CASE(initialization_lists_reject_missing_targets_and_incompatible_elements) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    auto* module = engine->GetModule("invalid-init-list");
    module->AddScriptSection("invalid-init-list",
        "int run() { auto missing = {1, 2}; array<int>@ mixed = {1, \"bad\"}; return 0; }");
    CHECK(!module->Build());
    bool target = false, element = false;
    for (const auto& diagnostic : diagnostics) {
        target = target || diagnostic.message.find(
            "initialization list requires a target object type") != std::string::npos;
        element = element || diagnostic.message.find("cannot initialize int element from string") !=
            std::string::npos;
    }
    CHECK(target);
    CHECK(element);
}

TEST_CASE(indexing_expressions_read_write_compound_and_increment_array_elements) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    auto* module = engine->GetModule("array-indexing");
    module->AddScriptSection("array-indexing",
        "int calls = 0; int next() { calls += 1; return 0; } "
        "int run() { array<int>@ values = {10, 20}; int old = values[next()]++; "
        "values[next()] += 9; values[1] = 10; "
        "return old + values[0] + values[1] + calls; }");
    if (!module->Build()) {
        std::string message;
        for (const auto& diagnostic : diagnostics)
            message += (message.empty() ? std::string{} : " | ") + diagnostic.message;
        throw std::runtime_error(message);
    }
    const auto* function = module->GetFunctionByDecl("int run()");
    CHECK(function != nullptr);
    std::size_t hostCalls = 0;
    for (const auto& instruction : function->code)
        if (instruction.opcode == mini_as::OpCode::CallHost) ++hostCalls;
    CHECK(hostCalls >= 9);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(function));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);

    std::stringstream archive(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveBytecode(archive));
    auto loadedEngine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptArray(*loadedEngine));
    auto* loaded = loadedEngine->GetModule("array-indexing-loaded");
    archive.seekg(0);
    CHECK(loaded->LoadBytecode(archive));
    auto loadedContext = loadedEngine->CreateContext();
    CHECK(loadedContext->Prepare(loaded->GetFunctionByDecl("int run()")));
    CHECK(loadedContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(loadedContext->GetReturnInt() == 42);
}

TEST_CASE(indexing_expressions_report_invalid_targets_and_bounds_locations) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    auto* invalid = engine->GetModule("invalid-index-target");
    invalid->AddScriptSection("invalid-index-target", "int run() { return 42[0]; }");
    CHECK(!invalid->Build());
    bool target = false;
    for (const auto& diagnostic : diagnostics)
        target = target || diagnostic.message.find("is not indexable") != std::string::npos;
    CHECK(target);

    auto* bounds = engine->GetModule("index-bounds");
    bounds->AddScriptSection("index-bounds",
        "int run() {\n  array<int>@ values = {1};\n  return values[2];\n}\n");
    CHECK(bounds->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(bounds->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("array index is out of range") !=
          std::string::npos);
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(foreach_uses_array_value_and_index_operator_protocol) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    auto* module = engine->GetModule("array-foreach");
    module->AddScriptSection("array-foreach",
        "int run() { array<int>@ values = {19, 99, 21}; int total = 0; "
        "foreach (auto value, auto index : values) { "
        "if (index == 1) continue; total += value + int(index); } "
        "int seen = 0; foreach (auto value : values) { seen++; if (seen == 2) break; } "
        "return total + seen - 2; }");
    if (!module->Build()) {
        std::string message;
        for (const auto& diagnostic : diagnostics)
            message += (message.empty() ? std::string{} : " | ") + diagnostic.message;
        throw std::runtime_error(message);
    }
    const auto* function = module->GetFunctionByDecl("int run()");
    CHECK(function != nullptr);
    std::size_t hostCalls = 0;
    for (const auto& instruction : function->code)
        if (instruction.opcode == mini_as::OpCode::CallHost) ++hostCalls;
    CHECK(hostCalls >= 8);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(function));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);

    std::stringstream archive(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveBytecode(archive));
    auto loadedEngine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptArray(*loadedEngine));
    auto* loaded = loadedEngine->GetModule("array-foreach-loaded");
    archive.seekg(0);
    CHECK(loaded->LoadBytecode(archive));
    auto loadedContext = loadedEngine->CreateContext();
    CHECK(loadedContext->Prepare(loaded->GetFunctionByDecl("int run()")));
    CHECK(loadedContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(loadedContext->GetReturnInt() == 42);
}

TEST_CASE(foreach_supports_script_operator_protocol_and_single_value_name) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("script-foreach");
    module->AddScriptSection("script-foreach",
        "class Range { uint opForBegin() { return 0; } "
        "bool opForEnd(uint it) { return it == 3; } "
        "uint opForNext(uint it) { return it + 1; } "
        "int opForValue(uint it) { return int(it) + 13; } } "
        "int run() { Range@ range = Range(); int total = 0; "
        "foreach (int value : range) total += value; return total; }");
    CHECK(module->Build());
    const auto* function = module->GetFunctionByDecl("int run()");
    CHECK(function != nullptr);
    std::size_t scriptCalls = 0;
    for (const auto& instruction : function->code)
        if (instruction.opcode == mini_as::OpCode::Call ||
            instruction.opcode == mini_as::OpCode::CallVirtual) ++scriptCalls;
    CHECK(scriptCalls >= 4);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(function));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(foreach_reports_invalid_protocol_and_runtime_locations) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* invalid = engine->GetModule("invalid-foreach");
    invalid->AddScriptSection("invalid-foreach",
        "class Incomplete { uint opForBegin() { return 0; } } "
        "int run() { Incomplete@ value = Incomplete(); "
        "foreach (auto item : value) {} return 0; }");
    CHECK(!invalid->Build());
    bool missingEnd = false;
    for (const auto& diagnostic : diagnostics)
        missingEnd = missingEnd || diagnostic.message.find("opForEnd") != std::string::npos;
    CHECK(missingEnd);

    auto* failing = engine->GetModule("failing-foreach");
    failing->AddScriptSection("failing-foreach",
        "class Broken {\n"
        "  uint opForBegin() { return 0; }\n"
        "  bool opForEnd(uint it) { return it == 1; }\n"
        "  uint opForNext(uint it) { return it + 1; }\n"
        "  int opForValue(uint it) { return 1 / int(it); }\n"
        "}\n"
        "int run() { Broken@ range = Broken(); foreach (auto value : range) {} return 0; }\n");
    CHECK(failing->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(failing->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("division by zero") != std::string::npos);
    CHECK(context->GetExceptionLocation().row == 5);
}

TEST_CASE(dictionary_addon_stores_converts_indexes_lists_and_iterates_values) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    CHECK(mini_as::addons::RegisterScriptDictionary(*engine));
    auto* module = engine->GetModule("dictionary-addon");
    module->AddScriptSection("dictionary-addon",
        "int run() { dictionary@ values = dictionary(); "
        "values.set(\"a\", int64(20)); values.set(\"b\", int64(21)); "
        "values.set(\"c\", int64(1)); int64 read = 0; "
        "bool found = values.get(\"a\", read); int64 indexed = int64(values[\"b\"]); "
        "array<string>@ keys = values.getKeys(); int64 total = 0; "
        "foreach (auto value, auto key : values) total += int64(value); "
        "bool erased = values.delete(\"c\"); "
        "bool valid = found && erased && read == 20 && indexed == 21 && "
        "keys.length() == 3 && values.getSize() == 2 && !values.exists(\"c\"); "
        "return valid ? int(total) : 0; }");
    if (!module->Build()) {
        std::string message;
        for (const auto& diagnostic : diagnostics)
            message += (message.empty() ? std::string{} : " | ") + diagnostic.message;
        throw std::runtime_error(message);
    }
    const auto* dictionaryType = engine->GetTypeInfo("dictionary");
    CHECK(dictionaryType != nullptr);
    CHECK(dictionaryType->collector != nullptr);
    const auto* function = module->GetFunctionByDecl("int run()");
    CHECK(function != nullptr);
    std::size_t hostCalls = 0;
    for (const auto& instruction : function->code)
        if (instruction.opcode == mini_as::OpCode::CallHost) ++hostCalls;
    CHECK(hostCalls >= 18);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(function));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);

    std::stringstream archive(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveBytecode(archive));
    auto loadedEngine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptArray(*loadedEngine));
    CHECK(mini_as::addons::RegisterScriptDictionary(*loadedEngine));
    auto* loaded = loadedEngine->GetModule("dictionary-loaded");
    archive.seekg(0);
    CHECK(loaded->LoadBytecode(archive));
    auto loadedContext = loadedEngine->CreateContext();
    CHECK(loadedContext->Prepare(loaded->GetFunctionByDecl("int run()")));
    CHECK(loadedContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(loadedContext->GetReturnInt() == 42);
}

TEST_CASE(dictionary_addon_requires_array_and_reports_bad_value_conversions) {
    auto missingArray = mini_as::CreateScriptEngine();
    CHECK(!mini_as::addons::RegisterScriptDictionary(*missingArray));

    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    CHECK(mini_as::addons::RegisterScriptDictionary(*engine));
    auto* invalid = engine->GetModule("invalid-dictionary-call");
    invalid->AddScriptSection("invalid-dictionary-call",
        "int run() { dictionary@ values = dictionary(); "
        "values.set(1, int64(2)); return 0; }");
    CHECK(!invalid->Build());
    bool invalidKey = false;
    for (const auto& diagnostic : diagnostics)
        invalidKey = invalidKey || diagnostic.message.find("no matching method for 'set'") !=
            std::string::npos;
    CHECK(invalidKey);

    auto* module = engine->GetModule("dictionary-conversion-error");
    module->AddScriptSection("dictionary-conversion-error",
        "int run() {\n"
        "  dictionary@ values = dictionary();\n"
        "  values.set(\"text\", \"not a number\");\n"
        "  return int(int64(values[\"text\"]));\n"
        "}\n");
    if (!module->Build()) {
        std::string message;
        for (const auto& diagnostic : diagnostics)
            message += (message.empty() ? std::string{} : " | ") + diagnostic.message;
        throw std::runtime_error(message);
    }
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("cannot convert to int64") != std::string::npos);
    CHECK(context->GetExceptionLocation().row == 4);
}

TEST_CASE(dictionary_addon_enumerates_cycles_for_collection) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    CHECK(mini_as::addons::RegisterScriptDictionary(*engine));
    const auto* type = engine->GetTypeInfo("dictionary");
    CHECK(type != nullptr);
    CHECK(type->collector != nullptr);
    mini_as::ObjectHandle handle(new mini_as::addons::ScriptDictionary(type));
    auto* dictionary = dynamic_cast<mini_as::addons::ScriptDictionary*>(handle.Get());
    CHECK(dictionary != nullptr);
    dictionary->Set("self", mini_as::Value(handle));
    handle = {};
    CHECK(engine->GetTrackedObjectCount() == 1);
    CHECK(engine->CollectGarbage() == 1);
    CHECK(engine->GetTrackedObjectCount() == 0);
}

TEST_CASE(any_and_ref_addons_store_retrieve_compare_and_round_trip) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptRef(*engine));
    CHECK(mini_as::addons::RegisterScriptAny(*engine));
    auto* module = engine->GetModule("any-ref-addon");
    module->AddScriptSection("any-ref-addon",
        "int run() { any@ box = any(int64(40)); int64 stored = 0; "
        "bool loaded = box.retrieve(stored); box.store(double(2)); double extra = 0; "
        "bool loadedExtra = box.retrieve(extra); ref first; ref second; "
        "bool refs = first == second && first.isNull(); "
        "return loaded && loadedExtra && refs ? int(stored + int64(extra)) : 0; }");
    CHECK(module->Build());
    const auto* function = module->GetFunctionByDecl("int run()");
    CHECK(function != nullptr);
    std::size_t hostCalls = 0;
    for (const auto& instruction : function->code)
        if (instruction.opcode == mini_as::OpCode::CallHost) ++hostCalls;
    CHECK(hostCalls >= 6);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(function));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);

    std::stringstream archive(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveBytecode(archive));
    auto loadedEngine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptRef(*loadedEngine));
    CHECK(mini_as::addons::RegisterScriptAny(*loadedEngine));
    auto* loadedModule = loadedEngine->GetModule("any-ref-loaded");
    archive.seekg(0);
    CHECK(loadedModule->LoadBytecode(archive));
    auto loadedContext = loadedEngine->CreateContext();
    CHECK(loadedContext->Prepare(loadedModule->GetFunctionByDecl("int run()")));
    CHECK(loadedContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(loadedContext->GetReturnInt() == 42);
}

TEST_CASE(ref_addon_bridges_arbitrary_registered_object_handles) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptRef(*engine));
    const auto* type = engine->RegisterObjectType("RefPayload");
    CHECK(type != nullptr);
    const bool factoryRegistered = engine->RegisterObjectFactory(
        "RefPayload", "RefPayload@ f(int value)",
        [type](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(
                new RefPayload(type, call.GetArgInt(0))));
        });
    CHECK(factoryRegistered);
    const bool methodRegistered = engine->RegisterObjectMethod(
        "RefPayload", "int get() const",
        [](mini_as::GenericCall& call) {
            auto* payload = dynamic_cast<RefPayload*>(call.GetObject().Get());
            CHECK(payload != nullptr);
            call.SetReturnInt(payload->value);
        });
    CHECK(methodRegistered);
    const bool wrapRegistered = engine->RegisterGlobalFunction(
        "ref Wrap(RefPayload@ value)",
        [](mini_as::GenericCall& call) {
            call.SetReturn(mini_as::addons::MakeScriptRef(call.GetArgObject(0)));
        });
    CHECK(wrapRegistered);
    const bool unwrapRegistered = engine->RegisterGlobalFunction(
        "RefPayload@ Unwrap(ref value)",
        [](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::addons::GetScriptRef(call.GetArg(0)));
        });
    CHECK(unwrapRegistered);
    auto* module = engine->GetModule("ref-bridge");
    module->AddScriptSection("ref-bridge",
        "int run() { RefPayload@ original = RefPayload(42); ref stored = Wrap(original); "
        "@original = null; RefPayload@ restored = Unwrap(stored); "
        "return stored.typeName() == \"RefPayload\" ? restored.get() : 0; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(any_and_ref_addons_report_invalid_overloads_and_collect_cycles) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(mini_as::addons::RegisterScriptRef(*engine));
    CHECK(mini_as::addons::RegisterScriptAny(*engine));
    auto* invalid = engine->GetModule("invalid-any-store");
    invalid->AddScriptSection("invalid-any-store",
        "class Node {} int run() { any@ value = any(); Node@ node = Node(); "
        "value.store(node); return 0; }");
    CHECK(!invalid->Build());
    bool rejected = false;
    for (const auto& diagnostic : diagnostics)
        rejected = rejected || diagnostic.message.find("no matching method for 'store'") !=
            std::string::npos;
    CHECK(rejected);

    const auto* anyType = engine->GetTypeInfo("any");
    CHECK(anyType != nullptr);
    mini_as::ObjectHandle anyHandle(new mini_as::addons::ScriptAny(anyType));
    auto* any = dynamic_cast<mini_as::addons::ScriptAny*>(anyHandle.Get());
    CHECK(any != nullptr);
    any->Store(mini_as::Value(anyHandle));
    anyHandle = {};
    CHECK(engine->CollectGarbage() == 1);

    auto* cycleModule = engine->GetModule("ref-cycle");
    cycleModule->AddScriptSection("ref-cycle",
        "class RefNode { ref link; } RefNode@ make() { return RefNode(); }");
    CHECK(cycleModule->Build());
    auto cycleContext = engine->CreateContext();
    CHECK(cycleContext->Prepare(cycleModule->GetFunctionByDecl("RefNode@ make()")));
    CHECK(cycleContext->Execute() == mini_as::ExecutionState::Finished);
    mini_as::ObjectHandle root = cycleContext->GetReturnValue().As<mini_as::ObjectHandle>();
    cycleContext.reset();
    auto* object = dynamic_cast<mini_as::ScriptObject*>(root.Get());
    CHECK(object != nullptr);
    object->SetField(0, mini_as::addons::MakeScriptRef(root));
    root = {};
    CHECK(engine->CollectGarbage() == 1);
    CHECK(engine->GetTrackedObjectCount() == 0);
}
