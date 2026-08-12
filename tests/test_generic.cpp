#include "test.hpp"
#include "mini_as/engine.hpp"

#include <sstream>

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

TEST_CASE(function_declaration_parser_preserves_registered_method_constness) {
    mini_as::DiagnosticSink diagnostics;
    const auto method = mini_as::ParseFunctionDeclaration("int get() const", diagnostics);
    CHECK(method.has_value());
    CHECK(method->name == "get");
    CHECK(method->returnType == mini_as::DataType::Int());
    CHECK(method->readOnlyMethod);
    CHECK(method->Declaration() == "int get() const");
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

TEST_CASE(registered_enums_typedefs_and_funcdefs_share_the_script_type_system) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterEnum("HostColor"));
    CHECK(engine->RegisterEnumValue("HostColor", "HostRed", 40));
    CHECK(engine->RegisterEnumValue("HostColor", "HostBlue", 42));
    CHECK(engine->RegisterTypedef("HostScore", mini_as::DataType::Int()));
    CHECK(engine->RegisterFuncdef("HostScore HostTransform(HostScore value)"));
    CHECK(engine->RegisterGlobalFunction("HostScore Lift(HostScore value)",
        [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetArgInt(0) + 2);
        }));
    auto* module = engine->GetModule("registered-named-types");
    module->AddScriptSection("registered-named-types",
        "HostColor selected = HostRed; HostTransform@ transform = @Lift; "
        "int main() { return transform(selected) + (selected == HostRed ? 0 : 100); }");
    CHECK(module->Build());
    const auto* function = module->GetFunctionByDecl("int main()");
    CHECK(function != nullptr);
    bool handleCall = false;
    for (const auto& instruction : function->code)
        handleCall = handleCall || instruction.opcode == mini_as::OpCode::CallHandle;
    CHECK(handleCall);
    CHECK(module->Bytecode().funcdefs.size() == 1);
    CHECK(module->Bytecode().funcdefs[0].name == "HostTransform");
    auto context = engine->CreateContext();
    CHECK(context->Prepare(function));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(registered_named_types_reject_duplicates_invalid_values_and_bad_callbacks) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(engine->RegisterEnum("HostColor"));
    CHECK(!engine->RegisterEnum("HostColor"));
    CHECK(!engine->RegisterEnumValue("Missing", "Red", 1));
    CHECK(!engine->RegisterEnumValue("HostColor", "", 1));
    CHECK(engine->RegisterEnumValue("HostColor", "Red", 40));
    CHECK(!engine->RegisterEnumValue("HostColor", "Red", 41));
    CHECK(!engine->RegisterTypedef("HostColor", mini_as::DataType::Int()));
    CHECK(!engine->RegisterTypedef("BadAlias", mini_as::DataType::String()));
    CHECK(engine->RegisterTypedef("HostScore", mini_as::DataType::Int()));
    CHECK(engine->RegisterFuncdef("HostScore HostTransform(HostScore value)"));
    CHECK(!engine->RegisterFuncdef("HostScore HostTransform(HostScore value)"));
    CHECK(!engine->RegisterFuncdef("not a declaration"));
    auto* module = engine->GetModule("bad-registered-named-types");
    module->AddScriptSection("bad-registered-named-types",
        "class HostColor {} float Wrong(float value) { return value; } "
        "int main() { HostTransform@ transform = @Wrong; return 0; }");
    CHECK(!module->Build());
    bool mismatch = false, collision = false;
    for (const auto& diagnostic : diagnostics) {
        mismatch = mismatch || diagnostic.message.find("no function matching a funcdef for 'Wrong'") !=
            std::string::npos;
        collision = collision || diagnostic.message.find("duplicate type 'HostColor'") !=
            std::string::npos;
    }
    CHECK(mismatch);
    CHECK(collision);
}

TEST_CASE(generic_declaration_parser_accepts_only_trailing_reference_variadics) {
    mini_as::DiagnosticSink diagnostics;
    const auto fixed = mini_as::ParseFunctionDeclaration(
        "int Sum(int seed, const int &in ...)", diagnostics);
    CHECK(fixed.has_value());
    CHECK(fixed->variadic);
    CHECK(fixed->parameters.size() == 2);
    CHECK(fixed->parameters.back() == mini_as::DataType::Int());
    CHECK(fixed->parameterModes.back() == mini_as::ParameterMode::In);
    CHECK(fixed->Declaration() == "int Sum(int, int &in ...)");

    mini_as::DiagnosticSink wildcardDiagnostics;
    const auto wildcard = mini_as::ParseFunctionDeclaration(
        "void Fill(? &out ...)", wildcardDiagnostics);
    CHECK(wildcard.has_value());
    CHECK(wildcard->parameters.back() == mini_as::DataType::Var());
    CHECK(wildcard->Declaration() == "void Fill(? &out ...)");

    mini_as::DiagnosticSink nonReferenceDiagnostics;
    const auto typedValue = mini_as::ParseFunctionDeclaration(
        "void Values(int ...)", nonReferenceDiagnostics);
    CHECK(typedValue.has_value());
    CHECK(typedValue->variadic);
    mini_as::DiagnosticSink inoutDiagnostics;
    CHECK(!mini_as::ParseFunctionDeclaration(
        "void Bad(? &inout ...)", inoutDiagnostics).has_value());
    mini_as::DiagnosticSink nonTrailingDiagnostics;
    CHECK(!mini_as::ParseFunctionDeclaration(
        "void Bad(int &in ..., int value)", nonTrailingDiagnostics).has_value());
    mini_as::DiagnosticSink bareWildcardDiagnostics;
    CHECK(!mini_as::ParseFunctionDeclaration(
        "void Bad(? &in)", bareWildcardDiagnostics).has_value());
}

TEST_CASE(generic_variadic_functions_support_zero_many_wildcard_and_exact_overloads) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction(
        "int Sum(int seed, const int &in ...)", [](mini_as::GenericCall& call) {
            int total = call.GetArgInt(0);
            for (std::size_t index = 1; index < call.GetArgCount(); ++index)
                total += call.GetArgInt(index);
            call.SetReturnInt(total);
        }));
    CHECK(engine->RegisterGlobalFunction(
        "int Kinds(const ? &in ...)", [](mini_as::GenericCall& call) {
            CHECK(call.GetArgCount() == 3);
            CHECK(call.GetArgType(0) == mini_as::DataType::Int());
            CHECK(call.GetArgType(1) == mini_as::DataType::String());
            CHECK(call.GetArgType(2) == mini_as::DataType::Bool());
            call.SetReturnInt(2);
        }));
    CHECK(engine->RegisterGlobalFunction(
        "int Pick(int value)", [](mini_as::GenericCall& call) {
            CHECK(call.GetArgCount() == 1);
            call.SetReturnInt(40);
        }));
    CHECK(engine->RegisterGlobalFunction(
        "int Pick(const int &in ...)", [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetArgCount() == 2 ? 2 : -100);
        }));

    auto* module = engine->GetModule("host-variadic");
    module->AddScriptSection("host-variadic.as",
        "int run() { return Sum(0, 20, 22) + "
        "Kinds(1, \"two\", true) + Pick(1) + Pick(1, 2) - 44; }");
    CHECK(module->Build());
    const auto* function = module->GetFunctionByDecl("int run()");
    CHECK(function != nullptr);
    bool sawTwoArguments = false, sawMany = false;
    for (const auto& instruction : function->code) {
        if (instruction.opcode != mini_as::OpCode::CallHost || instruction.operand < 0) continue;
        const auto& callable = module->Bytecode().callables.at(
            static_cast<std::size_t>(instruction.operand));
        sawTwoArguments = sawTwoArguments || callable.parameterCount == 2;
        sawMany = sawMany || callable.parameterCount == 3;
    }
    CHECK(sawTwoArguments);
    CHECK(sawMany);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(function));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);

    auto* invalid = engine->GetModule("missing-variadic-tail");
    invalid->AddScriptSection("missing-variadic-tail.as", "int run() { return Sum(0); }");
    CHECK(!invalid->Build());
}

TEST_CASE(generic_wildcard_out_variadics_preserve_actual_lvalue_types) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction(
        "void Fill(? &out ...)", [](mini_as::GenericCall& call) {
            CHECK(call.GetArgType(0) == mini_as::DataType::Int());
            CHECK(call.GetArgType(1) == mini_as::DataType::String());
            call.SetArgInt(0, 40);
            call.SetArgString(1, "ok");
        }));
    auto* module = engine->GetModule("wildcard-out");
    module->AddScriptSection("wildcard-out.as",
        "int run() { int number; string text; Fill(number, text); "
        "return number + (text == \"ok\" ? 2 : 0); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(generic_variadic_out_requires_lvalues_and_reports_bad_host_write_location) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(engine->RegisterGlobalFunction(
        "void Fill(? &out ...)", [](mini_as::GenericCall& call) {
            call.SetArgString(0, "wrong");
        }));
    auto* invalid = engine->GetModule("bad-variadic-out");
    invalid->AddScriptSection("bad-variadic-out.as", "void run() { Fill(1); }");
    CHECK(!invalid->Build());
    bool lvalueDiagnostic = false;
    for (const auto& diagnostic : diagnostics)
        lvalueDiagnostic = lvalueDiagnostic ||
            diagnostic.message.find("assignable lvalues") != std::string::npos;
    CHECK(lvalueDiagnostic);

    auto* runtime = engine->GetModule("bad-variadic-write");
    runtime->AddScriptSection("bad-variadic-write.as",
        "int run() {\n  int value; Fill(value);\n  return value;\n}");
    CHECK(runtime->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(runtime->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("wrote string to int") != std::string::npos);
    CHECK(context->GetExceptionLocation().section == "bad-variadic-write.as");
    CHECK(context->GetExceptionLocation().row == 2);
}

TEST_CASE(generic_variadic_signatures_round_trip_through_bytecode_archives) {
    auto registerSum = [](mini_as::ScriptEngine& engine) {
        return engine.RegisterGlobalFunction(
            "int Sum(int seed, int ...)", [](mini_as::GenericCall& call) {
                int total = 0;
                for (std::size_t index = 0; index < call.GetArgCount(); ++index)
                    total += call.GetArgInt(index);
                call.SetReturnInt(total);
            });
    };
    auto sourceEngine = mini_as::CreateScriptEngine();
    CHECK(registerSum(*sourceEngine));
    auto* source = sourceEngine->GetModule("variadic-bytecode-source");
    source->AddScriptSection("variadic-bytecode.as",
        "int run() { return Sum(2, 20, 20); }");
    CHECK(source->Build());
    std::stringstream archive(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(source->SaveBytecode(archive));

    auto targetEngine = mini_as::CreateScriptEngine();
    CHECK(registerSum(*targetEngine));
    auto* target = targetEngine->GetModule("variadic-bytecode-loaded");
    archive.seekg(0);
    CHECK(target->LoadBytecode(archive));
    bool foundVariadic = false;
    for (const auto& host : target->Bytecode().hostFunctions) {
        const auto* function = target->Bytecode().FindHostFunction(host.first);
        foundVariadic = foundVariadic || (function && function->signature.variadic);
    }
    CHECK(foundVariadic);
    auto context = targetEngine->CreateContext();
    CHECK(context->Prepare(target->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(registered_variadic_funcdefs_dispatch_host_function_handles) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterFuncdef("int Collector(int ...)"));
    CHECK(engine->RegisterGlobalFunction(
        "int Collect(int ...)", [](mini_as::GenericCall& call) {
            int total = 0;
            for (std::size_t index = 0; index < call.GetArgCount(); ++index)
                total += call.GetArgInt(index);
            call.SetReturnInt(total);
        }));
    auto* module = engine->GetModule("variadic-funcdef");
    module->AddScriptSection("variadic-funcdef.as",
        "Collector@ collector = @Collect; int run() { return collector(20, 22); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(registered_template_functions_instantiate_explicit_generic_calls) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterFuncdef("int IntUnary(int value)"));
    CHECK(engine->RegisterGlobalFunction(
        "T Identity<class T>(T value)", [](mini_as::GenericCall& call) {
            CHECK(call.GetTemplateArgCount() == 1);
            CHECK(call.GetTemplateArgType(0) == call.GetArgType(0));
            call.SetReturn(call.GetArg(0));
        }));
    auto* module = engine->GetModule("template-functions");
    module->AddScriptSection("template-functions.as",
        "int run() { IntUnary@ copy = @Identity<int>; return copy(40) + "
        "(Identity<string>(\"ok\") == \"ok\" ? 2 : 0); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);

    std::size_t instances = 0;
    for (std::size_t index = 0; index < engine->GetFunctionMetadataCount(); ++index) {
        const auto* metadata = engine->GetFunctionMetadataByIndex(index);
        if (metadata && metadata->signature.name == "Identity" &&
            !metadata->signature.templateArguments.empty()) ++instances;
    }
    CHECK(instances == 2);
}

TEST_CASE(registered_template_functions_support_namespaces_multiple_types_and_bytecode) {
    auto registerTemplates = [](mini_as::ScriptEngine& engine) {
        if (!engine.SetDefaultNamespace("HostTools")) return false;
        const bool registered = engine.RegisterGlobalFunction(
            "T Select<T, U>(T first, U second)", [](mini_as::GenericCall& call) {
                CHECK(call.GetTemplateArgCount() == 2);
                CHECK(call.GetTemplateArgType(0) == mini_as::DataType::Int());
                CHECK(call.GetTemplateArgType(1) == mini_as::DataType::String());
                CHECK(call.GetArgType(0) == mini_as::DataType::Int());
                CHECK(call.GetArgType(1) == mini_as::DataType::String());
                call.SetReturn(call.GetArg(0));
            });
        return engine.SetDefaultNamespace("") && registered;
    };
    auto sourceEngine = mini_as::CreateScriptEngine();
    CHECK(registerTemplates(*sourceEngine));
    auto* source = sourceEngine->GetModule("template-function-bytecode");
    source->AddScriptSection("template-function-bytecode.as",
        "int run() { return HostTools::Select<int, string>(42, \"ignored\"); }");
    CHECK(source->Build());
    std::stringstream archive(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(source->SaveBytecode(archive));

    auto targetEngine = mini_as::CreateScriptEngine();
    CHECK(registerTemplates(*targetEngine));
    auto* loaded = targetEngine->GetModule("template-function-bytecode-loaded");
    archive.seekg(0);
    CHECK(loaded->LoadBytecode(archive));
    auto context = targetEngine->CreateContext();
    CHECK(context->Prepare(loaded->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(registered_template_functions_support_overloaded_definitions) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction(
        "T Select<T>(T value)", [](mini_as::GenericCall& call) {
            call.SetReturn(call.GetArg(0));
        }));
    CHECK(engine->RegisterGlobalFunction(
        "T Select<T>(T first, T second)", [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetArgInt(0) + call.GetArgInt(1));
        }));
    auto* module = engine->GetModule("overloaded-template-functions");
    module->AddScriptSection("overloaded-template-functions.as",
        "int run() { return Select<int>(20) + Select<int>(10, 12); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(registered_template_function_diagnostics_reject_invalid_uses_and_collisions) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(!engine->RegisterGlobalFunction(
        "T Broken<T, T>(T value)", [](mini_as::GenericCall&) {}));
    CHECK(engine->RegisterGlobalFunction(
        "T Identity<T>(T value)", [](mini_as::GenericCall& call) {
            call.SetReturn(call.GetArg(0));
        }));

    auto* deduction = engine->GetModule("template-function-deduction");
    deduction->AddScriptSection("deduction.as",
        "int run() { return Identity(42); }");
    CHECK(!deduction->Build());

    CHECK(engine->RegisterGlobalFunction(
        "int Identity(int value)", [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetArgInt(0));
        }));

    auto* wrongCount = engine->GetModule("template-function-wrong-count");
    wrongCount->AddScriptSection("wrong-count.as",
        "int run() { return Identity<int, float>(42); }");
    CHECK(!wrongCount->Build());

    auto* omitted = engine->GetModule("template-function-omitted-types");
    omitted->AddScriptSection("omitted-types.as",
        "int run() { return Identity(42); }");
    CHECK(omitted->Build());
    auto omittedContext = engine->CreateContext();
    CHECK(omittedContext->Prepare(omitted->GetFunctionByDecl("int run()")));
    CHECK(omittedContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(omittedContext->GetReturnInt() == 42);

    auto* collision = engine->GetModule("template-function-collision");
    collision->AddScriptSection("collision.as",
        "int run() { return Identity<int>(42); }");
    CHECK(!collision->Build());

    CHECK(engine->RegisterGlobalFunction(
        "T@ HandleOnly<T>(T@ value)", [](mini_as::GenericCall& call) {
            call.SetReturn(call.GetArg(0));
        }));
    auto* invalidHandle = engine->GetModule("template-function-invalid-handle");
    invalidHandle->AddScriptSection("invalid-handle.as",
        "int run() { return HandleOnly<int>(42); }");
    CHECK(!invalidHandle->Build());
    CHECK(diagnostics.size() >= 4);
}

TEST_CASE(registered_template_function_exceptions_keep_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction(
        "T Fail<T>(T value)", [](mini_as::GenericCall& call) {
            CHECK(call.GetTemplateArgType(0) == mini_as::DataType::Int());
            call.SetException("template callback failed");
        }));
    auto* module = engine->GetModule("template-function-exception");
    module->AddScriptSection("template-function-exception.as",
        "int run() {\n  return Fail<int>(42);\n}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "template callback failed");
    CHECK(context->GetExceptionLocation().section == "template-function-exception.as");
    CHECK(context->GetExceptionLocation().row == 2);
}

TEST_CASE(generic_declarations_parse_qualified_and_nested_template_types) {
    mini_as::DiagnosticSink diagnostics;
    const auto signature = mini_as::ParseFunctionDeclaration(
        "Outer::Box<Inner::Pair<int,float>>@ Wrap(Outer::Box<int>@ value)",
        diagnostics);
    CHECK(signature.has_value());
    CHECK(!diagnostics.HasErrors());
    CHECK(signature->returnType ==
          mini_as::DataType::Object("Outer::Box<Inner::Pair<int,float>>", true));
    CHECK(signature->parameters.size() == 1);
    CHECK(signature->parameters[0] ==
          mini_as::DataType::Object("Outer::Box<int>", true));
}

