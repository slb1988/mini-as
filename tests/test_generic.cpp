#include "test.hpp"
#include "mini_as/engine.hpp"

TEST_CASE(generic_host_bridge_moves_typed_arguments_and_return) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction("float Scale(float value)", [](mini_as::GenericCall& call) {
        CHECK(call.GetArgCount() == 1);
        call.SetReturnFloat(call.GetArgFloat(0) * 2.5f);
    }));
    auto* module = engine->GetModule("host");
    module->AddScriptSection("host", "float run(int x) { return Scale(x) + 1; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->SetArgInt(0, 4));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnFloat() == 11.0f);
}

TEST_CASE(generic_host_exception_becomes_script_exception) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction("int Fail()", [](mini_as::GenericCall& call) {
        call.SetException("host rejected the operation");
    }));
    auto* module = engine->GetModule("host-error");
    module->AddScriptSection("host-error", "int run() { return Fail(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "host rejected the operation");
    CHECK(context->GetExceptionLocation().section == "host-error");
}

TEST_CASE(registration_parser_rejects_invalid_and_duplicate_declarations) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> messages;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& message) { messages.push_back(message); });
    auto callback = [](mini_as::GenericCall&) {};
    CHECK(!engine->RegisterGlobalFunction("not a declaration", callback));
    CHECK(engine->RegisterGlobalFunction("void Print(string &in)", callback));
    CHECK(!engine->RegisterGlobalFunction("void Print(string)", callback));
    CHECK(messages.size() >= 2);
}

TEST_CASE(generic_call_supports_double_arguments_and_returns) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction("double Twice(double)", [](mini_as::GenericCall& call) {
        call.SetReturnDouble(call.GetArgDouble(0) * 2.0);
    }));
    auto* module = engine->GetModule("generic-double");
    module->AddScriptSection("script", "double run(double value) { return Twice(value); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->SetArgDouble(0, 21.0));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnDouble() == 42.0);
}

TEST_CASE(generic_call_writes_out_and_inout_arguments_back_to_script) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction(
        "void Update(int &out result, int &inout total)",
        [](mini_as::GenericCall& call) {
            call.SetArgInt(0, 40);
            call.SetArgInt(1, call.GetArgInt(1) + 2);
        }));
    auto* module = engine->GetModule("host-references");
    module->AddScriptSection("host-references",
        "int main() { int result; int total = 40; Update(result, total); "
        "return result + total - 40; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

