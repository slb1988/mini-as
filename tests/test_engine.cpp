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

