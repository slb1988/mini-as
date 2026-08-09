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

TEST_CASE(global_property_declaration_parser_preserves_type_name_and_constness) {
    mini_as::DiagnosticSink diagnostics;
    const auto mutableProperty = mini_as::ParseGlobalPropertyDeclaration(
        "uint64 ticks", diagnostics);
    const auto constantProperty = mini_as::ParseGlobalPropertyDeclaration(
        "const string applicationName", diagnostics);
    CHECK(mutableProperty.has_value());
    CHECK(mutableProperty->name == "ticks");
    CHECK(mutableProperty->type == mini_as::DataType::UInt64());
    CHECK(mutableProperty->host);
    CHECK(!mutableProperty->isConst);
    CHECK(constantProperty.has_value());
    CHECK(constantProperty->name == "applicationName");
    CHECK(constantProperty->type == mini_as::DataType::String());
    CHECK(constantProperty->isConst);
    CHECK(!mini_as::ParseGlobalPropertyDeclaration("void missing", diagnostics).has_value());
    CHECK(!mini_as::ParseGlobalPropertyDeclaration("int value = 1", diagnostics).has_value());
}

TEST_CASE(registered_global_properties_are_live_and_support_reference_writeback) {
    auto engine = mini_as::CreateScriptEngine();
    mini_as::Value counter(std::int32_t{40});
    CHECK(engine->RegisterGlobalProperty("int hostCounter", &counter));
    CHECK(engine->RegisterGlobalFunction("void Set(int &out value)",
        [](mini_as::GenericCall& call) { call.SetArgInt(0, 42); }));
    auto* module = engine->GetModule("host-properties");
    module->AddScriptSection("host-properties",
        "int read() { return hostCounter; } "
        "int update() { hostCounter += 1; Set(hostCounter); return hostCounter; }");
    CHECK(module->Build());

    counter = mini_as::Value(std::int32_t{41});
    auto readContext = engine->CreateContext();
    CHECK(readContext->Prepare(module->GetFunctionByDecl("int read()")));
    CHECK(readContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(readContext->GetReturnInt() == 41);

    auto updateContext = engine->CreateContext();
    CHECK(updateContext->Prepare(module->GetFunctionByDecl("int update()")));
    CHECK(updateContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(updateContext->GetReturnInt() == 42);
    CHECK(counter.As<std::int32_t>() == 42);
}

TEST_CASE(registered_global_properties_validate_declarations_storage_and_names) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    mini_as::Value integer(std::int32_t{1});
    mini_as::Value text("mini-as");
    CHECK(!engine->RegisterGlobalProperty("not a declaration", &integer));
    CHECK(!engine->RegisterGlobalProperty("int nullStorage", nullptr));
    CHECK(!engine->RegisterGlobalProperty("int wrongType", &text));
    CHECK(engine->RegisterGlobalProperty("int shared", &integer));
    CHECK(!engine->RegisterGlobalProperty("int shared", &integer));
    CHECK(diagnostics.size() >= 4);
}

TEST_CASE(const_registered_global_properties_reject_assignment) {
    auto engine = mini_as::CreateScriptEngine();
    mini_as::Value limit(std::int32_t{42});
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(engine->RegisterGlobalProperty("const int hostLimit", &limit));
    auto* module = engine->GetModule("const-host-property");
    module->AddScriptSection("const-host-property",
        "int run() { hostLimit = 1; return hostLimit; }");
    CHECK(!module->Build());
    bool protectedAssignment = false;
    for (const auto& diagnostic : diagnostics)
        protectedAssignment = protectedAssignment ||
            diagnostic.message.find("cannot assign to const variable") != std::string::npos;
    CHECK(protectedAssignment);
}

TEST_CASE(script_globals_cannot_shadow_registered_global_properties) {
    auto engine = mini_as::CreateScriptEngine();
    mini_as::Value counter(std::int32_t{40});
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(engine->RegisterGlobalProperty("int hostCounter", &counter));
    auto* module = engine->GetModule("host-property-collision");
    module->AddScriptSection("host-property-collision", "int hostCounter = 1;");
    CHECK(!module->Build());
    bool duplicate = false;
    for (const auto& diagnostic : diagnostics)
        duplicate = duplicate ||
            diagnostic.message.find("duplicate variable 'hostCounter'") != std::string::npos;
    CHECK(duplicate);
}

TEST_CASE(registered_global_property_type_drift_reports_script_location) {
    auto engine = mini_as::CreateScriptEngine();
    mini_as::Value counter(std::int32_t{40});
    CHECK(engine->RegisterGlobalProperty("int hostCounter", &counter));
    auto* module = engine->GetModule("host-property-drift");
    module->AddScriptSection("host-property-drift",
        "int run() {\n"
        "  return hostCounter;\n"
        "}\n");
    CHECK(module->Build());
    counter = mini_as::Value("wrong type");
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("registered global property type changed") !=
        std::string::npos);
    CHECK(context->GetExceptionLocation().section == "host-property-drift");
    CHECK(context->GetExceptionLocation().row == 2);
}

