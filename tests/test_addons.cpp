#include "test.hpp"
#include "mini_as/addons/array.hpp"

#include <sstream>

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
