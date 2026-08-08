#include "test.hpp"
#include "mini_as/engine.hpp"

TEST_CASE(engine_module_context_pipeline_executes_function) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("math", mini_as::ModulePolicy::AlwaysCreate);
    module->AddScriptSection("declaration", "int calc(int a,");
    module->AddScriptSection("body", "int b) { return a * b + 1; }");
    CHECK(module->Build());
    const auto* function = module->GetFunctionByDecl("int calc(int, int)");
    CHECK(function != nullptr);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(function));
    CHECK(context->SetArgInt(0, 6));
    CHECK(context->SetArgInt(1, 7));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 43);
}

TEST_CASE(engine_forwards_build_diagnostics_and_honors_module_policy) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> messages;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& message) { messages.push_back(message); });
    CHECK(engine->GetModule("missing", mini_as::ModulePolicy::OnlyIfExists) == nullptr);
    auto* module = engine->GetModule("bad");
    module->AddScriptSection("broken", "int f() { return unknown; }");
    CHECK(!module->Build());
    CHECK(!messages.empty());
    CHECK(messages[0].location.section == "broken");
}

TEST_CASE(two_pass_functions_support_forward_calls_and_recursion) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("functions");
    module->AddScriptSection("functions",
        "int entry(int n) { return factorial(n); }"
        "int factorial(int n) { if (n <= 1) return 1; return n * factorial(n - 1); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int entry(int)")));
    CHECK(context->SetArgInt(0, 6));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 720);
}

TEST_CASE(function_call_stack_preserves_caller_locals) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("frames");
    module->AddScriptSection("frames",
        "int twice(int x) { return x * 2; } int calc(int x) { int saved = x + 1; return twice(x) + saved; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("calc")));
    CHECK(context->SetArgInt(0, 10));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 31);
}

TEST_CASE(failed_module_rebuild_preserves_last_successful_image) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("rebuild");
    module->AddScriptSection("good", "int value() { return 41; }");
    CHECK(module->Build());

    module->AddScriptSection("bad", "int value() { return missing; }");
    CHECK(!module->Build());

    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("value")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 41);
}

TEST_CASE(prepared_context_keeps_its_module_image_across_rebuild) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("snapshots");
    module->AddScriptSection("v1", "int value() { return 1; }");
    CHECK(module->Build());

    auto oldContext = engine->CreateContext();
    CHECK(oldContext->Prepare(module->GetFunctionByName("value")));

    module->AddScriptSection("v2", "int value() { return 2; }");
    CHECK(module->Build());

    auto newContext = engine->CreateContext();
    CHECK(newContext->Prepare(module->GetFunctionByName("value")));
    CHECK(oldContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(newContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(oldContext->GetReturnInt() == 1);
    CHECK(newContext->GetReturnInt() == 2);
}

TEST_CASE(module_rebuild_preserves_function_and_type_ids) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("stable-ids");
    module->AddScriptSection("v1", "class Box { int value; } int answer() { return 1; }");
    CHECK(module->Build());
    const auto functionId = module->GetFunctionByName("answer")->signature.id;
    const auto typeId = engine->GetTypeInfo("Box")->id;
    CHECK(functionId.IsValid());
    CHECK(typeId.IsValid());

    module->AddScriptSection("v2", "class Box { int value; } int answer() { return 2; }");
    CHECK(module->Build());
    CHECK(module->GetFunctionByName("answer")->signature.id == functionId);
    CHECK(engine->GetTypeInfo("Box")->id == typeId);
}

TEST_CASE(multiple_declarations_execute_in_source_order) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("multiple-declarations");
    module->AddScriptSection("success",
        "int value() { int first = 2, second = first + 3, third; third = second * 4; return third; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("value")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 20);
}

TEST_CASE(multiple_declarations_report_duplicate_names) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("duplicate-declarations");
    module->AddScriptSection("invalid", "int value() { int item = 1, item = 2; return item; }");
    CHECK(!module->Build());
    bool foundDuplicate = false;
    for (const auto& diagnostic : diagnostics)
        foundDuplicate = foundDuplicate || diagnostic.message.find("duplicate variable 'item'") != std::string::npos;
    CHECK(foundDuplicate);
}

TEST_CASE(const_locals_can_be_read_but_not_assigned) {
    auto engine = mini_as::CreateScriptEngine();
    auto* valid = engine->GetModule("const-valid");
    valid->AddScriptSection("success", "int value() { const int base = 6, factor = 7; return base * factor; }");
    CHECK(valid->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(valid->GetFunctionByName("value")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);

    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* invalid = engine->GetModule("const-invalid");
    invalid->AddScriptSection("invalid", "int value() { const int answer = 41; answer = 42; return answer; }");
    CHECK(!invalid->Build());
    bool protectedAssignment = false;
    for (const auto& diagnostic : diagnostics)
        protectedAssignment = protectedAssignment ||
            diagnostic.message.find("cannot assign to const variable 'answer'") != std::string::npos;
    CHECK(protectedAssignment);
}

