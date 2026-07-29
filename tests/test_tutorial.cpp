#include "test.hpp"
#include "mini_as/engine.hpp"

#include <cmath>

TEST_CASE(tutorial_clone_runs_registered_print_clock_and_float_return) {
    auto engine = mini_as::CreateScriptEngine();
    std::string output;
    CHECK(engine->RegisterGlobalFunction("void Print(string &in)", [&](mini_as::GenericCall& call) {
        output += call.GetArgString(0);
    }));
    CHECK(engine->RegisterGlobalFunction("int GetSystemTime()", [](mini_as::GenericCall& call) {
        call.SetReturnInt(2500);
    }));
    auto* module = engine->GetModule("tutorial-test");
    module->AddScriptSection("script.as",
        "float calc(float a, float b) {"
        " Print(\"Received: \" + a + \", \" + b + \"\\n\");"
        " Print(\"System has been running for \" + GetSystemTime()/1000.0 + \" seconds\\n\");"
        " return a*b; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("float calc(float, float)")));
    CHECK(context->SetArgFloat(0, 3.0f));
    CHECK(context->SetArgFloat(1, 2.5f));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(std::fabs(context->GetReturnFloat() - 7.5f) < 0.0001f);
    CHECK(output == "Received: 3, 2.5\nSystem has been running for 2.5 seconds\n");
}

TEST_CASE(context_line_callback_can_suspend_resume_and_abort) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("controls");
    module->AddScriptSection("controls", "int work() { int x = 1; x = x + 1; return x; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("work")));
    int callbacks = 0;
    context->SetLineCallback([&](mini_as::ScriptContext& current, const mini_as::SourceLocation&) {
        if (++callbacks == 2) current.Suspend();
    });
    CHECK(context->Execute() == mini_as::ExecutionState::Suspended);
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 2);

    CHECK(context->Prepare(module->GetFunctionByName("work")));
    context->SetLineCallback([](mini_as::ScriptContext& current, const mini_as::SourceLocation&) { current.Abort(); });
    CHECK(context->Execute() == mini_as::ExecutionState::Aborted);
}

TEST_CASE(runtime_exception_exposes_script_call_stack) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("stacktrace");
    module->AddScriptSection("stacktrace",
        "int inner() { return 1/0; } int middle() { return inner(); } int outer() { return middle(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("outer")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetCallStack().size() == 3);
    CHECK(context->GetCallStack()[0].functionDeclaration == "int inner()");
    CHECK(context->GetCallStack()[2].functionDeclaration == "int outer()");
}

