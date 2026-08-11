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

TEST_CASE(host_type_reflection_metadata_has_stable_addresses_and_complete_kinds) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterObjectType("HostBox") != nullptr);
    const auto* hostBox = engine->GetTypeMetadataByName("HostBox");
    CHECK(hostBox != nullptr);
    CHECK(hostBox->kind == mini_as::TypeMetadataKind::Object);
    CHECK(hostBox->host);
    CHECK(engine->RegisterObjectMethod("HostBox", "int read() const",
        [](mini_as::GenericCall& call) { call.SetReturnInt(42); }));
    CHECK(engine->GetTypeMetadataByName("HostBox") == hostBox);
    CHECK(hostBox->methods.size() == 1);
    CHECK(hostBox->methods[0].Declaration() == "int read() const");

    CHECK(engine->RegisterEnum("HostMode"));
    const auto* hostMode = engine->GetTypeMetadataByName("HostMode");
    CHECK(hostMode != nullptr);
    CHECK(engine->RegisterEnumValue("HostMode", "HostReady", 42));
    CHECK(engine->GetTypeMetadataByName("HostMode") == hostMode);
    CHECK(hostMode->kind == mini_as::TypeMetadataKind::Enum);
    CHECK(hostMode->enumValues.size() == 1);
    CHECK(hostMode->enumValues[0].value == 42);

    CHECK(engine->RegisterTypedef("HostScore", mini_as::DataType::Int()));
    CHECK(engine->RegisterFuncdef("HostScore HostCallback(HostScore value)"));
    CHECK(engine->GetTypeMetadataCount() == 4);
    for (std::size_t index = 0; index < engine->GetTypeMetadataCount(); ++index) {
        const auto* metadata = engine->GetTypeMetadataByIndex(index);
        CHECK(metadata != nullptr);
        CHECK(engine->GetTypeMetadataById(metadata->id) == metadata);
        CHECK(engine->GetTypeMetadataByName(metadata->name) == metadata);
    }
    const auto* alias = engine->GetTypeMetadataByName("HostScore");
    CHECK(alias != nullptr);
    CHECK(alias->kind == mini_as::TypeMetadataKind::Typedef);
    CHECK(alias->underlyingType == mini_as::DataType::Int());
    const auto* callback = engine->GetTypeMetadataByName("HostCallback");
    CHECK(callback != nullptr);
    CHECK(callback->kind == mini_as::TypeMetadataKind::Funcdef);
    CHECK(callback->funcdef.returnType == mini_as::DataType::Int());
    CHECK(engine->GetTypeMetadataByIndex(4) == nullptr);
    CHECK(engine->GetTypeMetadataById(mini_as::TypeId{}) == nullptr);
    CHECK(engine->GetTypeMetadataByName("Missing") == nullptr);
}

TEST_CASE(script_type_reflection_publishes_successful_rebuilds_atomically) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("type-reflection");
    module->AddScriptSection("v1",
        "class Box { int value; int read() { return value; } } "
        "enum Mode { Ready = 41, Done } typedef uint Bits; "
        "funcdef int Callback(int value); int main() { return Done; }");
    CHECK(module->Build());

    const auto* box = engine->GetTypeMetadataByName("Box");
    CHECK(box != nullptr);
    CHECK(!box->host);
    CHECK(box->kind == mini_as::TypeMetadataKind::Object);
    CHECK(box->fields.size() == 1);
    CHECK(box->methods.size() == 1);
    const auto* mode = engine->GetTypeMetadataByName("Mode");
    CHECK(mode != nullptr);
    CHECK(mode->enumValues.size() == 2);
    CHECK(mode->enumValues[1].value == 42);
    const auto* bits = engine->GetTypeMetadataByName("Bits");
    CHECK(bits != nullptr);
    CHECK(bits->underlyingType == mini_as::DataType::UInt());
    const auto* callback = engine->GetTypeMetadataByName("Callback");
    CHECK(callback != nullptr);
    CHECK(callback->funcdef.parameters == std::vector<mini_as::DataType>{mini_as::DataType::Int()});

    module->AddScriptSection("v2",
        "class Box { int value; int extra; int read() { return value + extra; } } "
        "int main() { return 42; }");
    CHECK(module->Build());
    CHECK(engine->GetTypeMetadataByName("Box") == box);
    CHECK(box->fields.size() == 2);

    module->AddScriptSection("failed",
        "class Box { float replaced; } int zero = 0; int bad = 1 / zero; "
        "int main() { return 0; }");
    CHECK(!module->Build());
    CHECK(engine->GetTypeMetadataByName("Box") == box);
    CHECK(box->fields.size() == 2);
    CHECK(box->fields[0].name == "value");
}

TEST_CASE(host_function_reflection_exposes_global_factory_and_method_metadata) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("HostBox");
    CHECK(type != nullptr);
    CHECK(engine->RegisterGlobalFunction("int HostAnswer(int value)",
        [](mini_as::GenericCall& call) { call.SetReturnInt(call.GetArgInt(0)); }));
    CHECK(engine->RegisterObjectFactory("HostBox", "HostBox@ f()",
        [](mini_as::GenericCall& call) { call.SetReturnObject({}); }));
    CHECK(engine->RegisterObjectMethod("HostBox", "int read() const",
        [](mini_as::GenericCall& call) { call.SetReturnInt(42); }));

    CHECK(engine->GetFunctionMetadataCount() == 3);
    bool global = false, factory = false, method = false;
    for (std::size_t index = 0; index < engine->GetFunctionMetadataCount(); ++index) {
        const auto* metadata = engine->GetFunctionMetadataByIndex(index);
        CHECK(metadata != nullptr);
        CHECK(metadata->signature.host);
        CHECK(metadata->moduleName.empty());
        CHECK(engine->GetFunctionMetadataById(metadata->id) == metadata);
        global = global || metadata->signature.name == "HostAnswer";
        factory = factory || metadata->signature.factory;
        method = method || (metadata->signature.method &&
                            metadata->signature.objectType == "HostBox");
    }
    CHECK(global);
    CHECK(factory);
    CHECK(method);
    CHECK(engine->GetFunctionMetadataByIndex(3) == nullptr);
    CHECK(engine->GetFunctionMetadataById(mini_as::FunctionId{}) == nullptr);
}

TEST_CASE(script_function_reflection_is_stable_and_failed_rebuilds_do_not_publish) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("function-reflection");
    module->AddScriptSection("v1",
        "class Box { Box() {} int read() const { return 1; } } "
        "int answer(int first) { return first; }");
    CHECK(module->Build());
    const auto* answer = module->GetFunctionMetadataByDecl("int answer(int)");
    CHECK(answer != nullptr);
    CHECK(answer->moduleName == "function-reflection");
    CHECK(!answer->signature.host);
    CHECK(answer->signature.parameterNames == std::vector<std::string>{"first"});
    const auto answerId = answer->id;
    const mini_as::FunctionMetadata* read = nullptr;
    for (std::size_t index = 0; index < engine->GetFunctionMetadataCount(); ++index) {
        const auto* candidate = engine->GetFunctionMetadataByIndex(index);
        if (candidate && candidate->signature.objectType == "Box" &&
            candidate->signature.name == "read") read = candidate;
    }
    CHECK(read != nullptr);
    CHECK(read->signature.method);

    module->AddScriptSection("v2",
        "class Box { Box() {} int read() const { return 2; } } "
        "int answer(int value) { return value + 1; }");
    CHECK(module->Build());
    CHECK(module->GetFunctionMetadataByDecl("int answer(int)") == answer);
    CHECK(engine->GetFunctionMetadataById(answerId) == answer);
    CHECK(answer->signature.parameterNames == std::vector<std::string>{"value"});
    CHECK(engine->GetFunctionMetadataById(read->id) == read);

    module->AddScriptSection("failed",
        "class Box { int read() const { return 3; } } int zero = 0; int bad = 1 / zero; "
        "int answer(int rejected) { return rejected; }");
    CHECK(!module->Build());
    CHECK(module->GetFunctionMetadataByDecl("int answer(int)") == answer);
    CHECK(answer->signature.parameterNames == std::vector<std::string>{"value"});
    CHECK(module->GetFunctionMetadataByDecl("int missing()") == nullptr);
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

TEST_CASE(auto_declarations_infer_initializer_types) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("auto-valid");
    module->AddScriptSection("success",
        "int value() { auto integer = 40; auto floating = integer + 0.5; const auto delta = 2; "
        "if (floating > 40.0) return integer + delta; return 0; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("value")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(auto_declarations_require_initializers) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("auto-invalid");
    module->AddScriptSection("invalid", "int value() { auto missing; return 0; }");
    CHECK(!module->Build());
    bool requiresInitializer = false;
    for (const auto& diagnostic : diagnostics)
        requiresInitializer = requiresInitializer ||
            diagnostic.message.find("auto declaration requires an initializer") != std::string::npos;
    CHECK(requiresInitializer);
}

TEST_CASE(module_globals_are_initialized_in_order_and_shared_by_contexts) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("globals");
    module->AddScriptSection("globals",
        "int first = 20, counter = first * 2; "
        "int next() { counter = counter + 1; return counter; }");
    CHECK(module->Build());

    auto firstCall = engine->CreateContext();
    auto secondCall = engine->CreateContext();
    CHECK(firstCall->Prepare(module->GetFunctionByName("next")));
    CHECK(secondCall->Prepare(module->GetFunctionByName("next")));
    CHECK(firstCall->Execute() == mini_as::ExecutionState::Finished);
    CHECK(secondCall->Execute() == mini_as::ExecutionState::Finished);
    CHECK(firstCall->GetReturnInt() == 41);
    CHECK(secondCall->GetReturnInt() == 42);
}

TEST_CASE(module_globals_are_visible_to_functions_declared_first) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("global-forward-reference");
    module->AddScriptSection("globals", "int read() { return answer; } const int answer = 42;");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("read")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(const_module_globals_reject_assignment) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("const-global");
    module->AddScriptSection("invalid", "const int answer = 41; int change() { answer = 42; return answer; }");
    CHECK(!module->Build());
    bool protectedAssignment = false;
    for (const auto& diagnostic : diagnostics)
        protectedAssignment = protectedAssignment ||
            diagnostic.message.find("cannot assign to const variable 'answer'") != std::string::npos;
    CHECK(protectedAssignment);
}

TEST_CASE(failed_global_initialization_preserves_previous_image_and_state) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("global-rebuild");
    module->AddScriptSection("v1", "int value = 40; int next() { value = value + 1; return value; }");
    CHECK(module->Build());
    auto first = engine->CreateContext();
    CHECK(first->Prepare(module->GetFunctionByName("next")));
    CHECK(first->Execute() == mini_as::ExecutionState::Finished);
    CHECK(first->GetReturnInt() == 41);

    module->AddScriptSection("v2", "int broken = 1 / 0; int next() { return broken; }");
    CHECK(!module->Build());
    auto preserved = engine->CreateContext();
    CHECK(preserved->Prepare(module->GetFunctionByName("next")));
    CHECK(preserved->Execute() == mini_as::ExecutionState::Finished);
    CHECK(preserved->GetReturnInt() == 42);
}

TEST_CASE(module_global_reflection_is_stable_scoped_and_published_atomically) {
    auto engine = mini_as::CreateScriptEngine();
    mini_as::Value hostCounter(std::int32_t{9});
    CHECK(engine->RegisterGlobalProperty("int hostCounter", &hostCounter));
    auto* module = engine->GetModule("global-reflection");
    module->AddScriptSection("v1",
        "int counter = 40; const int answer = 42; "
        "namespace nested { int value = 7; } int main() { return counter; }");
    CHECK(module->Build());

    CHECK(module->GetGlobalMetadataCount() == 3);
    const auto* counter = module->GetGlobalMetadataByIndex(0);
    const auto* answer = module->GetGlobalMetadataByDecl("const int answer");
    const auto* nested = module->GetGlobalMetadataByName("nested::value");
    CHECK(counter != nullptr);
    CHECK(counter->signature.name == "counter");
    CHECK(counter->signature.Declaration() == "int counter");
    CHECK(counter->moduleName == "global-reflection");
    CHECK(module->GetGlobalMetadataById(counter->id) == counter);
    CHECK(answer != nullptr);
    CHECK(answer->signature.isConst);
    CHECK(nested != nullptr);
    CHECK(nested->signature.type == mini_as::DataType::Int());
    CHECK(module->GetGlobalMetadataByName("hostCounter") == nullptr);
    CHECK(module->GetGlobalMetadataByIndex(3) == nullptr);
    CHECK(module->GetGlobalMetadataById(mini_as::GlobalId{}) == nullptr);
    CHECK(module->GetGlobalMetadataByDecl("int missing") == nullptr);

    const auto counterId = counter->id;
    module->AddScriptSection("v2",
        "const int counter = 41; int answer = 43; "
        "namespace nested { int value = 8; } int extra = 9; "
        "int main() { return counter; }");
    CHECK(module->Build());
    CHECK(module->GetGlobalMetadataCount() == 4);
    CHECK(module->GetGlobalMetadataById(counterId) == counter);
    CHECK(counter->signature.isConst);
    CHECK(counter->signature.Declaration() == "const int counter");
    CHECK(module->GetGlobalMetadataByDecl("int counter") == nullptr);
    CHECK(module->GetGlobalMetadataByDecl("const int counter") == counter);

    module->AddScriptSection("failed",
        "double counter = 1.5; int zero = 0; int bad = 1 / zero; "
        "int main() { return 0; }");
    CHECK(!module->Build());
    CHECK(module->GetGlobalMetadataById(counterId) == counter);
    CHECK(counter->signature.type == mini_as::DataType::Int());
    CHECK(counter->signature.isConst);
    CHECK(module->GetGlobalMetadataByName("bad") == nullptr);
}

TEST_CASE(dynamic_functions_compile_against_module_scope_and_preserve_snapshots) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("dynamic-functions");
    module->AddScriptSection("base",
        "int base = 10; int helper(int value, int increment = base + 1) { "
        "return value + increment; } "
        "class Box { int value; Box(int input) { value = input; } "
        "int read() { return value; } } int main() { return helper(0); }");
    CHECK(module->Build());
    const auto initializerSize = module->Bytecode().globalInitializer.code.size();

    auto oldContext = engine->CreateContext();
    CHECK(oldContext->Prepare(module->GetFunctionByDecl("int main()")));
    const auto* dynamic = module->CompileFunction("dynamic",
        "int dynamic(int input) { Box@ box = Box(input); "
        "return helper(box.read()); }");
    CHECK(dynamic != nullptr);
    CHECK(module->Bytecode().globalInitializer.code.size() == initializerSize);
    CHECK(module->GetFunctionByDecl("int dynamic(int)") == dynamic);
    CHECK(module->GetFunctionMetadataByDecl("int dynamic(int)") != nullptr);

    auto dynamicContext = engine->CreateContext();
    CHECK(dynamicContext->Prepare(dynamic));
    CHECK(dynamicContext->SetArgInt(0, 31));
    CHECK(dynamicContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(dynamicContext->GetReturnInt() == 42);
    CHECK(oldContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(oldContext->GetReturnInt() == 11);

    const auto* chain = module->CompileFunction(
        "chain", "int chain() { return dynamic(31); }");
    CHECK(chain != nullptr);
    auto chainContext = engine->CreateContext();
    CHECK(chainContext->Prepare(chain));
    CHECK(chainContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(chainContext->GetReturnInt() == 42);
    auto retainedDynamicContext = engine->CreateContext();
    CHECK(retainedDynamicContext->Prepare(dynamic));
    CHECK(retainedDynamicContext->SetArgInt(0, 31));
    CHECK(retainedDynamicContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(retainedDynamicContext->GetReturnInt() == 42);

    const auto* recursive = module->CompileFunction("recursive-added",
        "int countdown(int value) { if (value == 0) return 42; "
        "return countdown(value - 1); }");
    CHECK(recursive != nullptr);
    auto recursiveContext = engine->CreateContext();
    CHECK(recursiveContext->Prepare(recursive));
    CHECK(recursiveContext->SetArgInt(0, 3));
    CHECK(recursiveContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(recursiveContext->GetReturnInt() == 42);

    const auto* detached = module->CompileFunction(
        "detached", "int transient() { return chain() + 1; }", false);
    CHECK(detached != nullptr);
    CHECK(module->GetFunctionByDecl("int transient()") == nullptr);
    CHECK(module->GetFunctionMetadataByDecl("int transient()") == nullptr);
    auto detachedContext = engine->CreateContext();
    CHECK(detachedContext->Prepare(detached));
    CHECK(detachedContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(detachedContext->GetReturnInt() == 43);
}

TEST_CASE(dynamic_function_failures_are_atomic_and_report_source_locations) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("dynamic-errors");
    module->AddScriptSection("base", "int existing() { return 42; }");
    CHECK(module->Build());

    CHECK(module->CompileFunction(
        "two", "int first() { return 1; } int second() { return 2; }") == nullptr);
    CHECK(module->CompileFunction(
        "recursive", "int recurse() { return recurse(); }", false) == nullptr);
    CHECK(module->CompileFunction(
        "duplicate", "int existing() { return 0; }") == nullptr);
    CHECK(module->GetFunctionByDecl("int existing()") != nullptr);
    bool exactlyOne = false, detachedRecursion = false, duplicate = false;
    for (const auto& diagnostic : diagnostics) {
        exactlyOne = exactlyOne ||
            diagnostic.message.find("exactly one function") != std::string::npos;
        detachedRecursion = detachedRecursion ||
            diagnostic.message.find("detached dynamic function cannot call itself") !=
                std::string::npos;
        duplicate = duplicate ||
            diagnostic.message.find("duplicate function 'int existing()'") != std::string::npos;
    }
    CHECK(exactlyOne);
    CHECK(detachedRecursion);
    CHECK(duplicate);

    const auto* failing = module->CompileFunction("dynamic-failure",
        "int fail_dynamic(int divisor) {\n return 42 / divisor;\n}", true, 5);
    CHECK(failing != nullptr);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(failing));
    CHECK(context->SetArgInt(0, 0));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("division by zero") != std::string::npos);
    CHECK(context->GetExceptionLocation().section == "dynamic-failure");
    CHECK(context->GetExceptionLocation().row == 7);
}

TEST_CASE(removed_functions_leave_scope_but_existing_references_keep_executing) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("function-removal");
    module->AddScriptSection("base",
        "int target() { return 40; } int caller() { return target() + 2; }");
    CHECK(module->Build());
    const auto* removed = module->GetFunctionByDecl("int target()");
    CHECK(removed != nullptr);
    CHECK(module->RemoveFunction(removed));
    CHECK(module->GetFunctionByDecl("int target()") == nullptr);
    CHECK(module->GetFunctionMetadataByDecl("int target()") == nullptr);
    CHECK(!module->RemoveFunction(removed));
    CHECK(!module->RemoveFunction(nullptr));

    auto removedContext = engine->CreateContext();
    CHECK(removedContext->Prepare(removed));
    CHECK(removedContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(removedContext->GetReturnInt() == 40);
    auto callerContext = engine->CreateContext();
    CHECK(callerContext->Prepare(module->GetFunctionByDecl("int caller()")));
    CHECK(callerContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(callerContext->GetReturnInt() == 42);

    CHECK(module->CompileFunction(
        "unrelated", "int unrelated() { return 1; }") != nullptr);
    CHECK(module->GetFunctionByDecl("int target()") == nullptr);

    CHECK(module->CompileFunction(
        "hidden", "int cannot_see_removed() { return target(); }") == nullptr);
    bool hidden = false;
    for (const auto& diagnostic : diagnostics)
        hidden = hidden || diagnostic.message.find("target") != std::string::npos;
    CHECK(hidden);

    const auto* replacement = module->CompileFunction(
        "replacement", "int target() { return 41; }");
    CHECK(replacement != nullptr);
    CHECK(replacement != removed);
    CHECK(module->GetFunctionByDecl("int target()") == replacement);
    const auto* newCaller = module->CompileFunction(
        "new-caller", "int new_caller() { return target() + 1; }");
    CHECK(newCaller != nullptr);
    auto newCallerContext = engine->CreateContext();
    CHECK(newCallerContext->Prepare(newCaller));
    CHECK(newCallerContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(newCallerContext->GetReturnInt() == 42);

    auto preservedCallerContext = engine->CreateContext();
    CHECK(preservedCallerContext->Prepare(module->GetFunctionByDecl("int caller()")));
    CHECK(preservedCallerContext->Execute() == mini_as::ExecutionState::Finished);
    CHECK(preservedCallerContext->GetReturnInt() == 42);
}

TEST_CASE(detached_and_method_functions_cannot_be_removed_from_module_scope) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("invalid-function-removal");
    module->AddScriptSection("base",
        "class Box { int read() { return 42; } } int main() { return 0; }");
    CHECK(module->Build());
    const auto* detached = module->CompileFunction(
        "detached", "int detached_value() { return 42; }", false);
    CHECK(detached != nullptr);
    CHECK(!module->RemoveFunction(detached));
    const mini_as::BytecodeFunction* method = nullptr;
    for (const auto& candidate : module->Bytecode().functions)
        if (candidate.signature.method && candidate.signature.name == "read") method = &candidate;
    CHECK(method != nullptr);
    CHECK(!module->RemoveFunction(method));
}

TEST_CASE(for_loops_execute_initializer_condition_and_increment) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("for-loop");
    module->AddScriptSection("success",
        "int sum() { int total = 0; for (int i = 1; i <= 6; i = i + 1) total = total + i; return total; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("sum")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 21);
}

TEST_CASE(for_loop_conditions_must_be_boolean) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("for-invalid");
    module->AddScriptSection("invalid", "int value() { for (int i = 0; 3; i = i + 1) { } return 0; }");
    CHECK(!module->Build());
    bool rejectedCondition = false;
    for (const auto& diagnostic : diagnostics)
        rejectedCondition = rejectedCondition || diagnostic.message.find("condition must be bool") != std::string::npos;
    CHECK(rejectedCondition);
}

TEST_CASE(do_while_loops_execute_body_before_condition) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("do-while");
    module->AddScriptSection("success",
        "int count() { int value = 0; do { value = value + 1; } while (value < 4); "
        "do value = value + 10; while (false); return value; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("count")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 14);
}

TEST_CASE(do_while_conditions_must_be_boolean) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("do-while-invalid");
    module->AddScriptSection("invalid", "int value() { do { } while (1); return 0; }");
    CHECK(!module->Build());
    bool rejectedCondition = false;
    for (const auto& diagnostic : diagnostics)
        rejectedCondition = rejectedCondition || diagnostic.message.find("condition must be bool") != std::string::npos;
    CHECK(rejectedCondition);
}

TEST_CASE(switch_uses_integer_constant_cases_and_falls_through) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("switch");
    module->AddScriptSection("success",
        "int choose(int value) { int result = 0; switch (value) { "
        "case 1: result = 1; case 2: result = result + 2; "
        "case 3: result = result + 3; default: result = result + 4; } return result; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("choose")));
    CHECK(context->SetArgInt(0, 2));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 9);
}

TEST_CASE(switch_rejects_duplicate_and_nonconstant_cases) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("switch-invalid");
    module->AddScriptSection("invalid",
        "int choose(int value) { switch (value) { case 1 + 1: return 1; case 2: return 2; "
        "case value: return 3; } return 0; }");
    CHECK(!module->Build());
    bool duplicate = false;
    bool nonconstant = false;
    for (const auto& diagnostic : diagnostics) {
        duplicate = duplicate || diagnostic.message.find("duplicate case value") != std::string::npos;
        nonconstant = nonconstant ||
            diagnostic.message.find("case value must be an integer constant expression") != std::string::npos;
    }
    CHECK(duplicate);
    CHECK(nonconstant);
}

TEST_CASE(break_exits_the_nearest_loop_or_switch) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("break");
    module->AddScriptSection("success",
        "int value() { int total = 0; for (int i = 0; i < 5; i = i + 1) { "
        "switch (i) { case 2: break; default: total = total + 1; break; } "
        "total = total + 10; if (i == 3) break; } return total; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("value")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 43);
}

TEST_CASE(break_outside_loop_or_switch_is_rejected) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("break-invalid");
    module->AddScriptSection("invalid", "int value() { break; return 0; }");
    CHECK(!module->Build());
    bool rejected = false;
    for (const auto& diagnostic : diagnostics)
        rejected = rejected || diagnostic.message.find("break statement is not inside") != std::string::npos;
    CHECK(rejected);
}

TEST_CASE(continue_targets_each_loop_condition_or_increment) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("continue");
    module->AddScriptSection("success",
        "int value() { int total = 0; "
        "for (int i = 1; i <= 3; i = i + 1) { if (i == 2) continue; total = total + i; } "
        "int w = 0; while (w < 3) { w = w + 1; if (w == 2) continue; total = total + 10; } "
        "int d = 0; do { d = d + 1; if (d < 2) continue; total = total + 100; } while (d < 2); "
        "return total; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("value")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 124);
}

TEST_CASE(continue_outside_loop_is_rejected) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("continue-invalid");
    module->AddScriptSection("invalid", "int value() { continue; return 0; }");
    CHECK(!module->Build());
    bool rejected = false;
    for (const auto& diagnostic : diagnostics)
        rejected = rejected || diagnostic.message.find("continue statement is not inside a loop") != std::string::npos;
    CHECK(rejected);
}

TEST_CASE(compound_assignments_update_local_global_and_field_lvalues) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("compound");
    module->AddScriptSection("success",
        "int global = 3; class Box { int value; } int run() { int local = 4; Box@ box = Box(); "
        "box.value = 5; local += 2; global *= 2; box.value -= 1; return local + global + box.value; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 16);
}

TEST_CASE(compound_assignments_protect_const_and_report_runtime_location) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* invalid = engine->GetModule("compound-const");
    invalid->AddScriptSection("invalid", "int run() { const int value = 1; value += 2; return value; }");
    CHECK(!invalid->Build());

    auto* runtime = engine->GetModule("compound-runtime");
    runtime->AddScriptSection("runtime", "int run() {\n int value = 10;\n value /= 0;\n return value;\n}");
    CHECK(runtime->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(runtime->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "division by zero");
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(prefix_and_postfix_increment_preserve_expression_values) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("increment");
    module->AddScriptSection("success",
        "int global = 1; class Box { int value; } int run() { int local = 1; "
        "int prefix = ++local; int postfix = local++; Box@ box = Box(); box.value = 5; "
        "int fieldOld = box.value++; int globalNew = ++global; "
        "return local * 10000 + prefix * 1000 + postfix * 100 + fieldOld * 10 + globalNew; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 32252);
}

TEST_CASE(increment_rejects_const_and_non_lvalue_operands) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("increment-invalid");
    module->AddScriptSection("invalid",
        "int run() { const int fixed = 1; ++fixed; int value = 2; (value + 1)++; return value; }");
    CHECK(!module->Build());
    bool constRejected = false;
    bool lvalueRejected = false;
    for (const auto& diagnostic : diagnostics) {
        constRejected = constRejected || diagnostic.message.find("cannot modify const variable") != std::string::npos;
        lvalueRejected = lvalueRejected || diagnostic.message.find("increment operand is not assignable") != std::string::npos;
    }
    CHECK(constRejected);
    CHECK(lvalueRejected);
}

TEST_CASE(conditional_expressions_short_circuit_and_convert_branches) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("conditional");
    module->AddScriptSection("success",
        "int safe() { return true ? 7 : 1 / 0; } "
        "float select(bool flag) { return flag ? 42 : 2.5; }");
    CHECK(module->Build());
    auto safe = engine->CreateContext();
    CHECK(safe->Prepare(module->GetFunctionByName("safe")));
    CHECK(safe->Execute() == mini_as::ExecutionState::Finished);
    CHECK(safe->GetReturnInt() == 7);
    auto select = engine->CreateContext();
    CHECK(select->Prepare(module->GetFunctionByName("select")));
    CHECK(select->SetArgBool(0, true));
    CHECK(select->Execute() == mini_as::ExecutionState::Finished);
    CHECK(select->GetReturnFloat() == 42.0f);
}

TEST_CASE(conditional_expressions_require_bool_and_compatible_branches) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("conditional-invalid");
    module->AddScriptSection("invalid", "int run() { int bad = 1 ? 2 : 3; return true ? 1 : false; }");
    CHECK(!module->Build());
    bool conditionRejected = false;
    bool branchesRejected = false;
    for (const auto& diagnostic : diagnostics) {
        conditionRejected = conditionRejected || diagnostic.message.find("requires a bool condition") != std::string::npos;
        branchesRejected = branchesRejected || diagnostic.message.find("incompatible types") != std::string::npos;
    }
    CHECK(conditionRejected);
    CHECK(branchesRejected);
}

TEST_CASE(instance_methods_use_implicit_this_and_preserve_object_state) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("methods");
    module->AddScriptSection("success",
        "class Counter { int value; void set(int next) { value = next; } "
        "int add(int delta) { value += delta; return value; } "
        "int addTwice(int delta) { return add(delta) + add(delta); } } "
        "int run() { Counter@ counter = Counter(); counter.set(38); return counter.addTwice(2); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 82);
}

TEST_CASE(instance_method_calls_report_unknown_members) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("method-invalid");
    module->AddScriptSection("invalid",
        "class Counter { int value; } int run() { Counter@ counter = Counter(); return counter.missing(); }");
    CHECK(!module->Build());
    bool rejected = false;
    for (const auto& diagnostic : diagnostics)
        rejected = rejected || diagnostic.message.find("no matching method for 'missing'") != std::string::npos;
    CHECK(rejected);
}

TEST_CASE(overloaded_constructors_initialize_instances_by_arity) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("constructors");
    module->AddScriptSection("success",
        "class Box { int value; Box(int first) { value = first; } "
        "Box(int first, int second) { value = first + second; } int get() { return value; } } "
        "int run() { Box@ large = Box(40); Box@ small = Box(1, 1); return large.get() + small.get(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(constructors_reject_missing_overloads_and_duplicates) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) { diagnostics.push_back(diagnostic); });
    auto* module = engine->GetModule("constructor-invalid");
    module->AddScriptSection("invalid",
        "class Box { Box(int value) { } Box(int other) { } } int run() { Box@ box = Box(); return 0; }");
    CHECK(!module->Build());
    bool duplicate = false;
    bool missing = false;
    for (const auto& diagnostic : diagnostics) {
        duplicate = duplicate || diagnostic.message.find("duplicate method or constructor") != std::string::npos;
        missing = missing || diagnostic.message.find("no matching constructor for 'Box'") != std::string::npos;
    }
    CHECK(duplicate);
    CHECK(missing);
}

TEST_CASE(field_initializers_run_in_declaration_order) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("field-initializers");
    module->AddScriptSection("success",
        "class Box { int base = 20; int value = base * 2; "
        "int get() { return value; } } int run() { Box@ box = Box(); return box.get(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 40);
}

TEST_CASE(field_initializers_check_types_and_report_runtime_locations) {
    auto engine = mini_as::CreateScriptEngine();
    auto* invalid = engine->GetModule("field-invalid");
    invalid->AddScriptSection("invalid", "class Box { int value = \"wrong\"; }");
    CHECK(!invalid->Build());

    auto* runtime = engine->GetModule("field-runtime");
    runtime->AddScriptSection("runtime",
        "int zero = 0;\nclass Box {\n int value = 1 / zero;\n}\nint run() { Box@ box = Box(); return 0; }");
    CHECK(runtime->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(runtime->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "division by zero");
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(interface_handles_dispatch_to_the_runtime_object_type) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("virtual-dispatch");
    module->AddScriptSection("success",
        "interface IValue { int get(); } "
        "class First : IValue { int get() { return 40; } } "
        "class Second : IValue { int get() { return 2; } } "
        "int run() { IValue@ first = First(); IValue@ second = Second(); "
        "return first.get() + second.get(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    bool emittedVirtualCall = false;
    for (const auto& instruction : module->GetFunctionByName("run")->code)
        emittedVirtualCall = emittedVirtualCall || instruction.opcode == mini_as::OpCode::CallVirtual;
    CHECK(emittedVirtualCall);
}

TEST_CASE(null_interface_dispatch_reports_the_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("virtual-null");
    module->AddScriptSection("runtime",
        "interface IValue { int get(); }\nint read(IValue@ item) {\n return item.get();\n}");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("read")));
    CHECK(context->SetArgObject(0, mini_as::ObjectHandle{}));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "null virtual method receiver");
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(integer_family_preserves_width_signedness_and_wraparound) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("integer-family");
    module->AddScriptSection("success",
        "int run() { int8 sb = 127; sb++; uint8 ub = 255; ub++; "
        "int16 a = 1; uint16 b = 1; int32 c = 1; uint32 d = 1; "
        "int64 e = 1; uint64 f = 1; int64 sum = a + b + c + d + e + f; "
        "return sb == -128 && ub == 0 ? sum + 36 : 0; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(integer_family_rejects_bad_initializers_and_locates_division_by_zero) {
    auto engine = mini_as::CreateScriptEngine();
    auto* invalid = engine->GetModule("integer-invalid");
    invalid->AddScriptSection("invalid", "uint8 bad = \"not an integer\";");
    CHECK(!invalid->Build());

    auto* runtime = engine->GetModule("integer-runtime");
    runtime->AddScriptSection("runtime",
        "uint64 divide() {\n uint64 value = 1;\n uint64 zero = 0;\n return value / zero;\n}");
    CHECK(runtime->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(runtime->GetFunctionByName("divide")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "division by zero");
    CHECK(context->GetExceptionLocation().row == 4);
}

TEST_CASE(double_values_keep_precision_and_cross_the_embedding_api) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("double-precision");
    module->AddScriptSection("success",
        "double add(double left, double right) { return left + right; } "
        "int precise() { double value = 16777217; float narrowed = value; "
        "return value > narrowed ? 42 : 0; }");
    CHECK(module->Build());
    auto add = engine->CreateContext();
    CHECK(add->Prepare(module->GetFunctionByName("add")));
    CHECK(add->SetArgDouble(0, 40.125));
    CHECK(add->SetArgDouble(1, 1.875));
    CHECK(add->Execute() == mini_as::ExecutionState::Finished);
    CHECK(add->GetReturnDouble() == 42.0);
    auto precise = engine->CreateContext();
    CHECK(precise->Prepare(module->GetFunctionByName("precise")));
    CHECK(precise->Execute() == mini_as::ExecutionState::Finished);
    CHECK(precise->GetReturnInt() == 42);
}

TEST_CASE(double_values_reject_integer_returns_and_locate_division_by_zero) {
    auto engine = mini_as::CreateScriptEngine();
    auto* invalid = engine->GetModule("double-invalid");
    invalid->AddScriptSection("invalid", "int bad() { return 1.5; }");
    CHECK(!invalid->Build());

    auto* runtime = engine->GetModule("double-runtime");
    runtime->AddScriptSection("runtime",
        "double divide() {\n double one = 1; double zero = 0;\n return one / zero;\n}");
    CHECK(runtime->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(runtime->GetFunctionByName("divide")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "division by zero");
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(numeric_literals_cover_bases_suffixes_and_automatic_widths) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("numeric-literals");
    module->AddScriptSection("success",
        "int run() { uint binary = 0b101010; uint octal = 0o52; "
        "uint decimal = 0d42; uint hex = 0x2A; int64 wide = 2147483648; "
        "uint64 huge = 9223372036854775808; double real = 1.25e1; float single = 2.5f; "
        "return binary == 42 && octal == 42 && decimal == 42 && hex == 42 && "
        "huge == 0x8000000000000000 && wide == 2147483648 && "
        "real == 12.5 && single == 2.5f ? 42 : 0; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(numeric_literals_reject_missing_digits_and_uint64_overflow) {
    auto engine = mini_as::CreateScriptEngine();
    auto* missing = engine->GetModule("numeric-prefix-invalid");
    missing->AddScriptSection("invalid", "uint bad = 0x;");
    CHECK(!missing->Build());
    auto* overflow = engine->GetModule("numeric-overflow-invalid");
    overflow->AddScriptSection("invalid", "uint64 bad = 18446744073709551616;");
    CHECK(!overflow->Build());
}

TEST_CASE(bitwise_and_shift_operators_preserve_left_type_and_precedence) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("bitwise");
    module->AddScriptSection("success",
        "int run() { uint value = 0x0F; value |= 0x20; value ^= 0x01; value &= 0x2E; "
        "value <<= 1; value >>= 1; int negative = -8; int arithmetic = negative >>> 1; "
        "int logical = negative >> 1; int precedence = 1 | 2 ^ 3 & 1; "
        "return value == 46 && ~0 == -1 && arithmetic == -4 && logical == 2147483644 "
        "&& precedence == 3 ? 42 : 0; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(bitwise_operators_reject_floating_operands) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("bitwise-invalid");
    module->AddScriptSection("invalid", "int bad() { return 1.5f & 1; }");
    CHECK(!module->Build());
}

TEST_CASE(exponent_operators_are_left_associative_and_support_compound_assignment) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("power");
    module->AddScriptSection("success",
        "int run() { int associated = 2 ** 3 ** 2; int compound = 3; compound **= 3; "
        "double reciprocal = 2.0 ** -1; int truncated = 2 ** -1; "
        "return associated == 64 && compound == 27 && reciprocal == 0.5 && truncated == 0 ? 42 : 0; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(exponent_operators_reject_objects_and_locate_integer_overflow) {
    auto engine = mini_as::CreateScriptEngine();
    auto* invalid = engine->GetModule("power-invalid");
    invalid->AddScriptSection("invalid", "int bad() { return \"x\" ** 2; }");
    CHECK(!invalid->Build());
    auto* runtime = engine->GetModule("power-runtime");
    runtime->AddScriptSection("runtime", "int power(int base) {\n return base ** 2;\n}");
    CHECK(runtime->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(runtime->GetFunctionByName("power")));
    CHECK(context->SetArgInt(0, 50000));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "exponent overflow");
    CHECK(context->GetExceptionLocation().row == 2);
}

TEST_CASE(enums_execute_auto_and_constant_values_as_named_int32_types) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("enums");
    module->AddScriptSection("enums",
        "enum Color { Red = 2, Green, Blue = Green + 2 } "
        "Color selected = Blue; "
        "int main() { Color local = Green; switch (local) { "
        "case Red: return 0; case Green: return selected == Blue ? 42 : 0; default: return 0; } }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(enums_reject_duplicate_nonconstant_and_mutated_values) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-enums");
    module->AddScriptSection("bad-enums",
        "int runtime() { return 1; } enum Bad { Same, Same, Dynamic = runtime() } "
        "int main() { Same = 3; return 0; }");
    CHECK(!module->Build());
    bool duplicate = false, nonconstant = false, mutation = false;
    for (const auto& diagnostic : diagnostics) {
        duplicate = duplicate || diagnostic.message.find("duplicate enum value 'Same'") != std::string::npos;
        nonconstant = nonconstant ||
            diagnostic.message.find("enum value must be an integer constant expression") != std::string::npos;
        mutation = mutation || diagnostic.message.find("cannot assign to enum value 'Same'") != std::string::npos;
    }
    CHECK(duplicate);
    CHECK(nonconstant);
    CHECK(mutation);
}

TEST_CASE(typedefs_alias_primitive_types_across_function_and_variable_declarations) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("typedefs");
    module->AddScriptSection("typedefs",
        "typedef int Score; Score bonus = 2; "
        "Score add(Score value) { Score result = value + bonus; return result; } "
        "int main() { return add(40); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(typedefs_reject_duplicate_alias_names) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-typedefs");
    module->AddScriptSection("bad-typedefs",
        "typedef int Number; typedef float Number; int main() { return 0; }");
    CHECK(!module->Build());
    bool duplicate = false;
    for (const auto& diagnostic : diagnostics)
        duplicate = duplicate || diagnostic.message.find("duplicate typedef 'Number'") != std::string::npos;
    CHECK(duplicate);
}

TEST_CASE(namespaces_isolate_symbols_and_resolve_parent_and_explicit_names) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("namespaces");
    module->AddScriptSection("namespaces",
        "namespace Root { int base = 2; namespace Left { int value = 40; "
        "int read() { return value + base; } } namespace Right { int value = 1; } "
        "class Box { int value = 40; int read() { return value; } } "
        "enum Delta { Bonus = 2 } } "
        "int main() { Root::Box@ box = Root::Box(); return Root::Left::read() + "
        "Root::Right::value - 1 + box.read() + Root::Bonus - 42; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(namespaces_reject_unknown_qualified_symbols) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-namespaces");
    module->AddScriptSection("bad-namespaces", "int main() { return Missing::answer; }");
    CHECK(!module->Build());
    CHECK(!diagnostics.empty());
    bool unknown = false;
    for (const auto& diagnostic : diagnostics)
        unknown = unknown ||
            diagnostic.message.find("unknown variable 'Missing::answer'") != std::string::npos;
    CHECK(unknown);
}

TEST_CASE(default_arguments_support_declaring_namespaces_methods_and_constructors) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("defaults");
    module->AddScriptSection("defaults",
        "namespace Config { int bonus = 2; int add(int value, int delta = bonus) { return value + delta; } } "
        "class Box { int value; Box(int initial = 40) { value = initial; } "
        "int add(int delta = 2) { return value + delta; } } "
        "int main() { Box@ box = Box(); return Config::add(40) + box.add() - 42; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(default_arguments_check_types_and_report_runtime_locations) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* bad = engine->GetModule("bad-defaults");
    bad->AddScriptSection("bad-defaults", "int bad(int value = \"text\") { return value; }");
    CHECK(!bad->Build());
    bool wrongType = false;
    for (const auto& diagnostic : diagnostics)
        wrongType = wrongType || diagnostic.message.find("cannot initialize default argument") != std::string::npos;
    CHECK(wrongType);

    auto* runtime = engine->GetModule("runtime-defaults");
    runtime->AddScriptSection("runtime-defaults",
        "int zero = 0;\nint fail(int value = 1 / zero) { return value; }\nint main() { return fail(); }");
    CHECK(runtime->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(runtime->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "division by zero");
    CHECK(context->GetExceptionLocation().row == 2);
}

TEST_CASE(named_arguments_reorder_calls_and_fill_omitted_defaults) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("named-arguments");
    module->AddScriptSection("named-arguments",
        "int combine(int first, int second = 0, int third = 0) { return first + second + third; } "
        "class Box { int combine(int first, int second = 0) { return first + second; } } "
        "int main() { Box@ box = Box(); return combine(third: 2, first: 38) + "
        "box.combine(second: 2, first: 38) - 38; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(named_arguments_reject_unknown_and_duplicate_parameter_names) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-named-arguments");
    module->AddScriptSection("bad-named-arguments",
        "int add(int first, int second = 0) { return first + second; } "
        "int main() { return add(first: 20, first: 22) + add(missing: 1); }");
    CHECK(!module->Build());
    bool noMatch = false;
    for (const auto& diagnostic : diagnostics)
        noMatch = noMatch || diagnostic.message.find("no matching function for 'add'") != std::string::npos;
    CHECK(noMatch);
}

TEST_CASE(reference_parameters_copy_values_back_to_locals_globals_and_fields) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("reference-parameters");
    module->AddScriptSection("reference-parameters",
        "int globalValue = 1; "
        "void adjust(int &out doubled, int &in value, int &inout total) { "
        "doubled = value * 2; total += doubled; } "
        "class Box { int value = 0; void add(int &inout target) { target += 2; } } "
        "int main() { int total = 2; Box@ box = Box(); "
        "adjust(globalValue, 20, total); box.add(total); adjust(box.value, 1, total); "
        "return globalValue + total + box.value - 46; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(reference_field_receivers_are_evaluated_once_before_the_call) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("reference-receiver");
    module->AddScriptSection("reference-receiver",
        "class Box { int value = 0; } int probes = 0; "
        "Box@ select(Box@ box) { probes++; return box; } "
        "void fill(int &out value) { value = 42; } "
        "int main() { Box@ box = Box(); fill(select(box).value); "
        "return probes == 1 ? box.value : 0; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(reference_parameters_reject_non_lvalues_const_targets_and_writes_to_in) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-reference-parameters");
    module->AddScriptSection("bad-reference-parameters",
        "void input(int &in value) { value = 2; } "
        "void output(int &out value) { value = 1; } "
        "int main() { const int fixed = 0; output(1); output(fixed); return 0; }");
    CHECK(!module->Build());
    bool nonLvalue = false, constTarget = false, writeToIn = false;
    for (const auto& diagnostic : diagnostics) {
        nonLvalue = nonLvalue ||
            diagnostic.message.find("must be assignable lvalues") != std::string::npos;
        constTarget = constTarget ||
            diagnostic.message.find("const value cannot be passed") != std::string::npos;
        writeToIn = writeToIn ||
            diagnostic.message.find("cannot assign to const variable 'value'") != std::string::npos;
    }
    CHECK(nonLvalue);
    CHECK(constTarget);
    CHECK(writeToIn);
}

TEST_CASE(reference_parameter_runtime_errors_report_the_callee_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("reference-runtime");
    module->AddScriptSection("reference-runtime",
        "void fail(int &inout value) {\n value = value / 0;\n}\n"
        "int main() { int value = 1; fail(value); return value; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "division by zero");
    CHECK(context->GetExceptionLocation().row == 2);
}

TEST_CASE(return_references_modify_globals_and_fields_and_read_as_values) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("return-references");
    module->AddScriptSection("return-references",
        "int value = 1; int &access() { return value; } "
        "const int &read() { return value; } "
        "class Box { int field = 0; int &get() { return field; } } "
        "int main() { Box@ box = Box(); access() = 38; access() += 2; "
        "int before = access()++; ++access(); box.get() = 2; "
        "return before + read() + box.get() - 42; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(return_references_reject_locals_parameters_const_writes_and_host_registration) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-return-references");
    module->AddScriptSection("bad-return-references",
        "int global = 0; int &localRef() { int local = 0; return local; } "
        "int &parameterRef(int value) { return value; } "
        "const int &read() { return global; } int main() { read() = 1; return 0; }");
    CHECK(!module->Build());
    int lifetimeErrors = 0;
    bool constWrite = false;
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.message.find("sufficient lifetime") != std::string::npos) ++lifetimeErrors;
        constWrite = constWrite || diagnostic.message.find("cannot assign to const") != std::string::npos;
    }
    CHECK(lifetimeErrors >= 2);
    CHECK(constWrite);
    CHECK(!engine->RegisterGlobalFunction("int &hostRef()", [](mini_as::GenericCall&) {}));
}

TEST_CASE(return_reference_runtime_errors_report_the_return_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("return-reference-runtime");
    module->AddScriptSection("return-reference-runtime",
        "class Box { int value = 0; } Box@ box;\n"
        "int &fail() { return box.value; }\n"
        "int main() { return fail(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "null or non-script object reference target");
    CHECK(context->GetExceptionLocation().row == 2);
}

TEST_CASE(script_destructors_run_once_at_vm_safe_points) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<int> finalized;
    CHECK(engine->RegisterGlobalFunction("void Record(int value)",
        [&](mini_as::GenericCall& call) { finalized.push_back(call.GetArgInt(0)); }));
    auto* module = engine->GetModule("destructors");
    module->AddScriptSection("destructors",
        "class Resource { int id; Resource(int value) { id = value; } "
        "~Resource() { Record(id); } } "
        "void dispose() { Resource@ first = Resource(20); Resource@ second = Resource(22); } "
        "int main() { dispose(); return 42; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(finalized.size() == 2);
    CHECK(finalized[0] + finalized[1] == 42);
    CHECK(engine->CollectGarbage() == 0);
    CHECK(finalized.size() == 2);
}

TEST_CASE(script_destructors_reject_parameters_and_report_runtime_failures) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* invalid = engine->GetModule("bad-destructor");
    invalid->AddScriptSection("bad-destructor", "class Resource { ~Resource(int value) {} }");
    CHECK(!invalid->Build());
    bool rejectedParameters = false;
    for (const auto& diagnostic : diagnostics)
        rejectedParameters = rejectedParameters ||
            diagnostic.message.find("destructor cannot declare parameters") != std::string::npos;
    CHECK(rejectedParameters);

    diagnostics.clear();
    auto* runtime = engine->GetModule("failing-destructor");
    runtime->AddScriptSection("failing-destructor",
        "class Broken {\n~Broken() { int value = 1 / 0; }\n}\n"
        "int main() { Broken@ value = Broken(); return 42; }");
    CHECK(runtime->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(runtime->GetFunctionByDecl("int main()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    bool finalizerFailure = false;
    for (const auto& diagnostic : diagnostics) {
        finalizerFailure = finalizerFailure ||
            (diagnostic.location.row == 2 &&
             diagnostic.message.find("script destructor '~Broken' failed: division by zero") !=
                 std::string::npos);
    }
    CHECK(finalizerFailure);
}

TEST_CASE(script_destructors_keep_the_creating_module_bytecode_after_rebuild) {
    auto engine = mini_as::CreateScriptEngine();
    int finalizedVersion = 0;
    CHECK(engine->RegisterGlobalFunction("void Record(int value)",
        [&](mini_as::GenericCall& call) { finalizedVersion = call.GetArgInt(0); }));
    auto* module = engine->GetModule("destructor-snapshot");
    module->AddScriptSection("old-image",
        "class Resource { ~Resource() { Record(1); } } "
        "Resource@ make() { return Resource(); }");
    CHECK(module->Build());
    mini_as::ObjectHandle oldObject;
    {
        auto context = engine->CreateContext();
        CHECK(context->Prepare(module->GetFunctionByDecl("Resource@ make()")));
        CHECK(context->Execute() == mini_as::ExecutionState::Finished);
        oldObject = context->GetReturnValue().As<mini_as::ObjectHandle>();
    }
    module->AddScriptSection("new-image",
        "class Resource { ~Resource() { Record(2); } } "
        "Resource@ make() { return Resource(); }");
    CHECK(module->Build());
    oldObject = {};
    CHECK(engine->CollectGarbage() == 0);
    CHECK(finalizedVersion == 1);
}

TEST_CASE(single_inheritance_supports_construction_fields_base_calls_and_polymorphism) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<int> destructionOrder;
    CHECK(engine->RegisterGlobalFunction("void Record(int value)",
        [&](mini_as::GenericCall& call) { destructionOrder.push_back(call.GetArgInt(0)); }));
    auto* module = engine->GetModule("inheritance");
    module->AddScriptSection("inheritance",
        "class Base { int value = 1; Base(int input) { value = input; } "
        "int score() { return value; } ~Base() { Record(1); } } "
        "class Derived : Base { int bonus = 2; Derived() { super(40); } "
        "int score() { return value + bonus; } int baseScore() { return Base::score(); } "
        "~Derived() { Record(2); } } "
        "int evaluate(Base@ item) { return item.score(); } "
        "int run() { Derived@ item = Derived(); return evaluate(item) + item.baseScore() - 40; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(destructionOrder.size() == 2);
    CHECK(destructionOrder[0] == 2);
    CHECK(destructionOrder[1] == 1);
}

TEST_CASE(single_inheritance_implicitly_calls_default_base_constructor) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("implicit-base-constructor");
    module->AddScriptSection("implicit-base-constructor",
        "class Base { int value; Base() { value = 40; } int score() { return value; } } "
        "class Derived : Base { int score() { return value + 2; } } "
        "int run() { Base@ item = Derived(); return item.score(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(single_inheritance_reuses_base_implementations_for_interfaces) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("inherited-interface-implementation");
    module->AddScriptSection("inherited-interface-implementation",
        "interface IValue { int get(); } "
        "class Base { int get() { return 42; } } "
        "class Derived : Base, IValue {} "
        "int read(IValue@ value) { return value.get(); } "
        "int run() { return read(Derived()); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(single_inheritance_rejects_invalid_hierarchies_overrides_and_downcasts) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-inheritance");
    module->AddScriptSection("bad-inheritance",
        "class First {} class Second {} class Multiple : First, Second {} "
        "class CycleA : CycleB {} class CycleB : CycleA {} "
        "class Base { Base(int value) {} int get() { return 1; } } "
        "class BadOverride : Base { float get() { return 1.0f; } } "
        "int run() { Base@ base; BadOverride@ derived = base; return 0; }");
    CHECK(!module->Build());
    bool multiple = false, cycle = false, overrideMismatch = false;
    bool missingDefault = false, downcast = false;
    for (const auto& diagnostic : diagnostics) {
        multiple = multiple || diagnostic.message.find("multiple classes") != std::string::npos;
        cycle = cycle || diagnostic.message.find("cyclic class inheritance") != std::string::npos;
        overrideMismatch = overrideMismatch ||
            diagnostic.message.find("overriding method must preserve") != std::string::npos;
        missingDefault = missingDefault || diagnostic.message.find("has no default constructor") != std::string::npos;
        downcast = downcast || diagnostic.message.find("cannot initialize BadOverride@ with Base@") != std::string::npos;
    }
    CHECK(multiple);
    CHECK(cycle);
    CHECK(overrideMismatch);
    CHECK(missingDefault);
    CHECK(downcast);
}

TEST_CASE(null_base_class_virtual_dispatch_reports_the_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("base-virtual-null");
    module->AddScriptSection("base-virtual-null",
        "class Base { int score() { return 1; } }\n"
        "int read(Base@ item) {\n return item.score();\n}");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int read(Base@)")));
    CHECK(context->SetArgObject(0, mini_as::ObjectHandle{}));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "null virtual method receiver");
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(private_and_protected_members_respect_class_hierarchy_access) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("member-access");
    module->AddScriptSection("member-access",
        "class Base { private int secret = 1; protected int value; "
        "protected Base(int input) { value = input; } "
        "private int hidden() { return secret; } "
        "protected int score() { return value + hidden(); } "
        "int read() { return score(); } } "
        "class Derived : Base { Derived() { super(40); } "
        "int bump() { value += 2; return score(); } } "
        "int run() { Derived@ item = Derived(); return item.read() + item.bump(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 84);
}

TEST_CASE(private_and_protected_members_reject_unauthorized_access) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-member-access");
    module->AddScriptSection("bad-member-access",
        "class Base { private int secret; protected int guarded; private Base() {} "
        "private int hidden() { return 1; } protected int shielded() { return 2; } } "
        "class Derived : Base { Derived() { super(); } "
        "int breach() { return secret + hidden(); } } "
        "int run() { Base@ item = Base(); return item.guarded + item.shielded(); }");
    CHECK(!module->Build());
    bool privateConstructor = false, privateField = false, privateMethod = false;
    bool protectedField = false, protectedMethod = false;
    for (const auto& diagnostic : diagnostics) {
        privateConstructor = privateConstructor ||
            diagnostic.message.find("private constructor 'Base::Base'") != std::string::npos;
        privateField = privateField ||
            diagnostic.message.find("private field 'Base::secret'") != std::string::npos;
        privateMethod = privateMethod ||
            diagnostic.message.find("private method 'Base::hidden'") != std::string::npos;
        protectedField = protectedField ||
            diagnostic.message.find("protected field 'Base::guarded'") != std::string::npos;
        protectedMethod = protectedMethod ||
            diagnostic.message.find("protected method 'Base::shielded'") != std::string::npos;
    }
    CHECK(privateConstructor);
    CHECK(privateField);
    CHECK(privateMethod);
    CHECK(protectedField);
    CHECK(protectedMethod);
}

TEST_CASE(reference_casts_use_runtime_class_and_interface_identity) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("reference-casts");
    module->AddScriptSection("reference-casts",
        "interface IValue { int score(); } "
        "class Base { int score() { return 1; } } "
        "class Derived : Base, IValue { int score() { return 42; } } "
        "int run() { Derived@ original = Derived(); Base@ good = original; "
        "Base@ bad = Base(); IValue@ iface = original; "
        "Derived@ fromBase = cast<Derived>(good); "
        "Derived@ failed = cast<Derived>(bad); "
        "Derived@ fromInterface = cast<Derived>(iface); "
        "Derived@ empty = cast<Derived>(null); "
        "return (fromBase is original ? fromBase.score() : 0) + fromInterface.score() + "
        "(failed is null ? 0 : 100) + (empty is null ? 0 : 100); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 84);
}

TEST_CASE(reference_casts_reject_non_object_targets_sources_and_unknown_types) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterObjectType("Host") != nullptr);
    CHECK(engine->RegisterGlobalFunction("Host@ GetHost()", [](mini_as::GenericCall& call) {
        call.SetReturnObject({});
    }));
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-reference-casts");
    module->AddScriptSection("bad-reference-casts",
        "class Base {} int run() { cast<int>(42); cast<Base>(42); cast<Base>(GetHost()); "
        "Base@ value = cast<Missing>(Base()); return 0; }");
    CHECK(!module->Build());
    bool target = false, source = false, hostSource = false, unknown = false;
    for (const auto& diagnostic : diagnostics) {
        target = target || diagnostic.message.find("target must be a class or interface") != std::string::npos;
        source = source || diagnostic.message.find("source must be an object handle") != std::string::npos;
        hostSource = hostSource ||
            diagnostic.message.find("source must be a script object handle") != std::string::npos;
        unknown = unknown || diagnostic.message.find("unknown reference cast target 'Missing'") != std::string::npos;
    }
    CHECK(target);
    CHECK(source);
    CHECK(hostSource);
    CHECK(unknown);
}

TEST_CASE(dereferencing_a_failed_reference_cast_reports_the_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("failed-reference-cast");
    module->AddScriptSection("failed-reference-cast",
        "class Base {}\nclass Derived : Base { int score() { return 42; } }\n"
        "int run() { Base@ item = Base();\n return cast<Derived>(item).score();\n}");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "null virtual method receiver");
    CHECK(context->GetExceptionLocation().row == 4);
}

TEST_CASE(class_operator_overloads_lower_to_virtual_method_calls) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("operator-overloads");
    module->AddScriptSection("operator-overloads",
        "class Label { int value; Label(int input) { value = input; } int read() { return value; } } "
        "class CompareOnly { int value; CompareOnly(int input) { value = input; } "
        "int opCmp(CompareOnly@ other) { return value - other.value; } } "
        "class Number { int value; Number(int input) { value = input; } "
        "Number@ opAdd(Number@ other) { return Number(value + other.value); } "
        "int opAdd_r(int left) { return left + value; } "
        "int opNeg() { return -value; } int opCom() { return ~value; } "
        "bool opEquals(Number@ other) { return value == other.value; } "
        "int opCmp(Number@ other) { return value - other.value; } "
        "int opCmp(int other) { return value - other; } "
        "int opAddAssign(int delta) { value += delta; return value; } "
        "int opPreInc() { value++; return value; } "
        "int opPostInc() { int before = value; value++; return before; } "
        "int opCall(int scale) { return value * scale; } "
        "Label@ opCast() { return Label(value); } } "
        "int run() { Number@ first = Number(20); Number@ second = Number(22); "
        "Number@ sum = first + second; int reverse = 20 + second; "
        "int assigned = (first += 2); int prefix = ++first; int postfix = first++; "
        "Label@ label = cast<Label>(first); "
        "Number@ twin = Number(24); bool equalByValue = first == twin; bool sameHandle = first is twin; "
        "CompareOnly@ equalLeft = CompareOnly(7); CompareOnly@ equalRight = CompareOnly(7); "
        "return sum.value + reverse + (-second) + (~Number(0)) + "
        "(first == second ? 1 : 0) + (first >= second ? 1 : 0) + (20 < second ? 1 : 0) + "
        "assigned + prefix + postfix + first(1) + label.read() + "
        "(equalByValue && !sameHandle ? 10 : 0) + "
        "(equalLeft == equalRight && !(equalLeft != equalRight) ? 10 : 0); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 199);
}

TEST_CASE(operator_overloads_cover_binary_reverse_assignment_and_decrement_tables) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("operator-table");
    module->AddScriptSection("operator-table",
        "class Ops { "
        "int opAdd(int v) { return 1; } int opSub(int v) { return 2; } "
        "int opMul(int v) { return 3; } int opDiv(int v) { return 4; } "
        "int opMod(int v) { return 5; } int opPow(int v) { return 6; } "
        "int opAnd(int v) { return 7; } int opOr(int v) { return 8; } "
        "int opXor(int v) { return 9; } int opShl(int v) { return 10; } "
        "int opShr(int v) { return 11; } int opUShr(int v) { return 12; } "
        "int opAdd_r(int v) { return 13; } int opSub_r(int v) { return 14; } "
        "int opMul_r(int v) { return 15; } int opDiv_r(int v) { return 16; } "
        "int opMod_r(int v) { return 17; } int opPow_r(int v) { return 18; } "
        "int opAnd_r(int v) { return 19; } int opOr_r(int v) { return 20; } "
        "int opXor_r(int v) { return 21; } int opShl_r(int v) { return 22; } "
        "int opShr_r(int v) { return 23; } int opUShr_r(int v) { return 24; } "
        "int opAssign(Ops@ v) { return 25; } int opAddAssign(int v) { return 26; } "
        "int opSubAssign(int v) { return 27; } int opMulAssign(int v) { return 28; } "
        "int opDivAssign(int v) { return 29; } int opModAssign(int v) { return 30; } "
        "int opPowAssign(int v) { return 31; } int opAndAssign(int v) { return 32; } "
        "int opOrAssign(int v) { return 33; } int opXorAssign(int v) { return 34; } "
        "int opShlAssign(int v) { return 35; } int opShrAssign(int v) { return 36; } "
        "int opUShrAssign(int v) { return 37; } "
        "int opPreDec() { return 38; } int opPostDec() { return 39; } } "
        "int run() { Ops@ value = Ops(); Ops@ other = Ops(); return "
        "(value + 0) + (value - 0) + (value * 0) + (value / 1) + (value % 1) + "
        "(value ** 1) + (value & 0) + (value | 0) + (value ^ 0) + "
        "(value << 0) + (value >> 0) + (value >>> 0) + "
        "(0 + value) + (0 - value) + (0 * value) + (1 / value) + (1 % value) + "
        "(1 ** value) + (0 & value) + (0 | value) + (0 ^ value) + "
        "(0 << value) + (0 >> value) + (0 >>> value) + "
        "(value = other) + (value += 0) + (value -= 0) + (value *= 0) + "
        "(value /= 1) + (value %= 1) + (value **= 1) + (value &= 0) + "
        "(value |= 0) + (value ^= 0) + (value <<= 0) + (value >>= 0) + "
        "(value >>>= 0) + (--value) + (value--); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 780);
}

TEST_CASE(operator_conversion_overloads_select_the_requested_return_type) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("operator-conversions");
    module->AddScriptSection("operator-conversions",
        "class Label { int value; Label(int input) { value = input; } int read() { return value; } } "
        "class Wrapped { int value; Wrapped(int input) { value = input; } "
        "int opConv() { return value; } string opConv() { return \"wrapped\"; } "
        "Label@ opConv() { return Label(value + 1); } } "
        "class Implicit { int value; Implicit(int input) { value = input; } "
        "int opImplConv() { return value; } } "
        "class CastSource { int value; CastSource(int input) { value = input; } "
        "Label@ opImplCast() { return Label(value); } } "
        "int run() { Wrapped@ value = Wrapped(41); int number = int(value); "
        "string text = string(value); Label@ label = Label(value); "
        "int implicitNumber = Implicit(3); "
        "Label@ implicitLabel = CastSource(2); int truncated = int(4.75); "
        "string digits = string(12); "
        "return number + (text == \"wrapped\" ? 1 : 0) + label.read() + implicitNumber + "
        "implicitLabel.read() + truncated + (digits == \"12\" ? 1 : 0); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 94);
}

TEST_CASE(operator_overloads_reject_missing_invalid_and_inaccessible_methods) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-operator-overloads");
    module->AddScriptSection("bad-operator-overloads",
        "class Missing {} "
        "class BadCompare { int opEquals(BadCompare@ other) { return 1; } "
        "bool opCmp(BadCompare@ other) { return true; } } "
        "class Hidden { private int opAdd(int value) { return value; } } "
        "class NoConversion {} "
        "int run() { Missing() + 1; BadCompare@ a = BadCompare(); "
        "BadCompare@ b = BadCompare(); bool same = a == b; "
        "int converted = int(NoConversion()); Hidden() + 1; return converted; }");
    CHECK(!module->Build());
    bool missing = false, invalidComparison = false, inaccessible = false, missingConversion = false;
    for (const auto& diagnostic : diagnostics) {
        missing = missing || diagnostic.message.find("no matching operator overload for '+'") != std::string::npos;
        invalidComparison = invalidComparison ||
            diagnostic.message.find("no matching operator overload for '=='") != std::string::npos;
        inaccessible = inaccessible ||
            diagnostic.message.find("private method 'Hidden::opAdd'") != std::string::npos;
        missingConversion = missingConversion ||
            diagnostic.message.find("no matching conversion operator to 'int'") != std::string::npos;
    }
    CHECK(missing);
    CHECK(invalidComparison);
    CHECK(inaccessible);
    CHECK(missingConversion);
}

TEST_CASE(operator_overload_null_receivers_report_the_operator_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("null-operator-receiver");
    module->AddScriptSection("null-operator-receiver",
        "class Number { int opAdd(int value) { return value; } }\n"
        "int run() {\n Number@ value = null;\n return value + 42;\n}");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "null virtual method receiver");
    CHECK(context->GetExceptionLocation().row == 4);
}

TEST_CASE(property_accessors_support_compact_explicit_and_virtual_access) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("property-accessors");
    module->AddScriptSection("property-accessors",
        "interface IValue { int amount { get const; set; } } "
        "class Meter : IValue { private int stored; Meter() { stored = 1; } "
        "int amount { get const { return stored; } set { stored = value * 2; } } "
        "int get_raw() const property { return stored; } "
        "void set_raw(int input) property { stored = input; } "
        "int bumpRaw() { raw += 1; return raw; } } "
        "Meter@ shared = Meter(); int receiverCalls = 0; "
        "Meter@ select() { receiverCalls++; return shared; } "
        "int run() { IValue@ view = shared; int inner = shared.bumpRaw(); "
        "view.amount = 10; view.amount += 1; select().raw += 3; "
        "return view.amount + shared.raw + receiverCalls + inner; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 93);
}

TEST_CASE(property_accessors_reject_invalid_read_write_and_declarations) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-property-accessors");
    module->AddScriptSection("bad-property-accessors",
        "class Bad { int get_readOnly() property { return 1; } "
        "void set_writeOnly(int value) property {} "
        "int get_mismatch() property { return 1; } "
        "void set_mismatch(string value) property {} "
        "int get_indexed(int index) property { return index; } "
        "int get_plain() { return 1; } } "
        "class Hidden { private int get_secret() property { return 1; } } "
        "int run() { Bad@ value = Bad(); value.readOnly = 2; int a = value.writeOnly; "
        "value.readOnly++; Hidden().secret; return value.plain; }");
    CHECK(!module->Build());
    bool readOnly = false, writeOnly = false, mismatch = false;
    bool indexed = false, unmarked = false, increment = false, inaccessible = false;
    for (const auto& diagnostic : diagnostics) {
        readOnly = readOnly || diagnostic.message.find("is read-only") != std::string::npos;
        writeOnly = writeOnly || diagnostic.message.find("is write-only") != std::string::npos;
        mismatch = mismatch || diagnostic.message.find("getter and setter types must match") != std::string::npos;
        indexed = indexed || diagnostic.message.find("indexed or malformed property accessor") != std::string::npos;
        unmarked = unmarked || diagnostic.message.find("has no field or property 'plain'") != std::string::npos;
        increment = increment || diagnostic.message.find("increment and decrement are not supported") != std::string::npos;
        inaccessible = inaccessible || diagnostic.message.find("private property getter 'Hidden::secret'") != std::string::npos;
    }
    CHECK(readOnly);
    CHECK(writeOnly);
    CHECK(mismatch);
    CHECK(indexed);
    CHECK(unmarked);
    CHECK(increment);
    CHECK(inaccessible);
}

TEST_CASE(property_accessor_null_receivers_report_the_access_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("null-property-receiver");
    module->AddScriptSection("null-property-receiver",
        "class Value { int get_number() property { return 1; } }\n"
        "int run() {\n Value@ value = null;\n return value.number;\n}");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "null virtual method receiver");
    CHECK(context->GetExceptionLocation().row == 4);
}

TEST_CASE(try_catch_recovers_from_local_and_called_function_exceptions) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("try-catch");
    module->AddScriptSection("try-catch",
        "int divide(int value) { return 100 / value; } "
        "int run() { int result = 1; try { result = divide(0); result = 99; } "
        "catch { result = 40; } try { try { result = divide(result - 40); } "
        "catch { result += 2; } } catch { result = -1; } "
        "try { result += 1; } catch { result = -2; } return result; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 43);
    CHECK(context->GetExceptionString().empty());
}

TEST_CASE(try_requires_a_following_catch_block) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-try-catch");
    module->AddScriptSection("bad-try-catch", "int run() { try { return 1; } return 2; }");
    CHECK(!module->Build());
    bool missingCatch = false;
    for (const auto& diagnostic : diagnostics)
        missingCatch = missingCatch || diagnostic.message.find("expected 'catch'") != std::string::npos;
    CHECK(missingCatch);
}

TEST_CASE(try_catch_recovers_from_host_exceptions_without_publishing_them) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction("int fail()", [](mini_as::GenericCall& call) {
        call.SetException("expected host failure");
    }));
    auto* module = engine->GetModule("host-try-catch");
    module->AddScriptSection("host-try-catch",
        "int run() { int result = 1; try { result = fail(); } catch { result = 42; } return result; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(context->GetExceptionString().empty());
}

TEST_CASE(funcdef_declarations_publish_stable_module_metadata) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("funcdefs");
    module->AddScriptSection("funcdefs",
        "funcdef void Notify(string message); "
        "namespace Events { funcdef bool Filter(int, int &inout value); } "
        "int run() { return 42; }");
    CHECK(module->Build());
    CHECK(module->Bytecode().funcdefs.size() == 2);
    CHECK(module->Bytecode().funcdefs[0].name == "Notify");
    CHECK(module->Bytecode().funcdefs[0].id.IsValid());
    CHECK(module->Bytecode().funcdefs[1].name == "Events::Filter");
    CHECK(module->Bytecode().funcdefs[1].id.IsValid());
    CHECK(module->Bytecode().funcdefs[0].id != module->Bytecode().funcdefs[1].id);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(funcdef_declarations_reject_duplicates_conflicts_and_defaults) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-funcdefs");
    module->AddScriptSection("bad-funcdefs",
        "funcdef void Same(int); funcdef int Same(float); "
        "class Conflict {} funcdef void Conflict(); "
        "funcdef void Defaults(int value = 1); int run() { return 0; }");
    CHECK(!module->Build());
    bool duplicate = false, defaultArgument = false;
    for (const auto& diagnostic : diagnostics) {
        duplicate = duplicate || diagnostic.message.find("duplicate or conflicting funcdef") != std::string::npos;
        defaultArgument = defaultArgument || diagnostic.message.find("cannot have default arguments") != std::string::npos;
    }
    CHECK(duplicate);
    CHECK(defaultArgument);
}

TEST_CASE(function_handles_store_reassign_pass_and_invoke_global_functions) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("function-handles");
    module->AddScriptSection("function-handles",
        "funcdef int Binary(int, int); "
        "int add(int a, int b) { return a + b; } "
        "int multiply(int a, int b) { return a * b; } "
        "int apply(Binary@ callback, int a, int b) { return callback(a, b); } "
        "int run() { Binary@ callback = @add; int first = callback(2, 3); "
        "@callback = @multiply; return first + apply(callback, 4, 5); }");
    CHECK(module->Build());
    bool emittedHandleCall = false;
    for (const auto& function : module->Bytecode().functions)
        for (const auto& instruction : function.code)
            emittedHandleCall = emittedHandleCall || instruction.opcode == mini_as::OpCode::CallHandle;
    CHECK(emittedHandleCall);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 25);
}

TEST_CASE(function_handles_invoke_registered_host_functions) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction("int hostAdd(int, int)", [](mini_as::GenericCall& call) {
        call.SetReturnInt(call.GetArgInt(0) + call.GetArgInt(1));
    }));
    auto* module = engine->GetModule("host-function-handle");
    module->AddScriptSection("host-function-handle",
        "funcdef int Binary(int, int); "
        "int run() { Binary@ callback = @hostAdd; return callback(20, 22); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(function_handles_work_in_module_globals_and_object_fields) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("stored-function-handles");
    module->AddScriptSection("stored-function-handles",
        "funcdef int Binary(int, int); "
        "int add(int a, int b) { return a + b; } "
        "int multiply(int a, int b) { return a * b; } "
        "Binary@ globalCallback = @add; "
        "class Holder { Binary@ callback = @multiply; "
        "int invoke() { return callback(6, 7); } } "
        "int run() { Holder@ holder = Holder(); return globalCallback(20, 22) + holder.invoke(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 84);
}

TEST_CASE(function_handles_reject_signature_mismatches) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-function-handle");
    module->AddScriptSection("bad-function-handle",
        "funcdef int Unary(int); int add(int a, int b) { return a + b; } "
        "int run() { Unary@ callback = @add; return 0; }");
    CHECK(!module->Build());
    bool mismatch = false;
    for (const auto& diagnostic : diagnostics)
        mismatch = mismatch || diagnostic.message.find("no function matching a funcdef") != std::string::npos;
    CHECK(mismatch);
}

TEST_CASE(null_function_handle_calls_report_the_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("null-function-handle");
    module->AddScriptSection("null-function-handle",
        "funcdef int Unary(int);\n"
        "int run() {\n"
        "  Unary@ callback;\n"
        "  if (!(callback is null)) return -1;\n"
        "  return callback(42);\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("null function handle") != std::string::npos);
    CHECK(context->GetExceptionLocation().row == 5);
}

TEST_CASE(delegates_bind_object_state_and_dispatch_virtual_overrides) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("delegates");
    module->AddScriptSection("delegates",
        "funcdef int Unary(int); "
        "class Base { int total; Base(int value) { total = value; } "
        "int apply(int value) { total += value; return total; } } "
        "class Derived : Base { Derived(int value) { super(value); } "
        "int apply(int value) { total += value * 2; return total; } } "
        "int run() { Base@ object = Derived(10); Unary@ callback = Unary(object.apply); "
        "return callback(5) * 100 + callback(6); }");
    CHECK(module->Build());
    bool emittedDelegate = false;
    for (const auto& function : module->Bytecode().functions)
        for (const auto& instruction : function.code)
            emittedDelegate = emittedDelegate || instruction.opcode == mini_as::OpCode::MakeDelegate;
    CHECK(emittedDelegate);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 2032);
}

TEST_CASE(delegates_keep_bound_objects_alive_after_local_handles_leave_scope) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("delegate-lifetime");
    module->AddScriptSection("delegate-lifetime",
        "funcdef int Unary(int); Unary@ callback; "
        "class Accumulator { int total; Accumulator(int value) { total = value; } "
        "int add(int value) { total += value; return total; } } "
        "void install() { Accumulator@ object = Accumulator(40); "
        "@callback = Unary(object.add); } "
        "int run() { install(); return callback(2); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(engine->GetTrackedObjectCount() == 1);
    CHECK(engine->CollectGarbage() == 0);
}

TEST_CASE(delegates_reject_mismatched_method_signatures) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-delegate");
    module->AddScriptSection("bad-delegate",
        "funcdef int Unary(int); class Target { int combine(int a, int b) { return a + b; } } "
        "int run() { Target@ object = Target(); Unary@ callback = Unary(object.combine); return 0; }");
    CHECK(!module->Build());
    bool mismatch = false;
    for (const auto& diagnostic : diagnostics)
        mismatch = mismatch || diagnostic.message.find("no method matching funcdef") != std::string::npos;
    CHECK(mismatch);
}

TEST_CASE(delegate_creation_from_a_null_object_reports_its_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("null-delegate");
    module->AddScriptSection("null-delegate",
        "funcdef int Unary(int);\n"
        "class Target { int apply(int value) { return value; } }\n"
        "int run() {\n"
        "  Target@ object = null;\n"
        "  Unary@ callback = Unary(object.apply);\n"
        "  return callback(42);\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("null object") != std::string::npos);
    CHECK(context->GetExceptionLocation().row == 5);
}

TEST_CASE(anonymous_functions_infer_funcdef_parameters_and_execute) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("anonymous-functions");
    module->AddScriptSection("anonymous-functions",
        "funcdef int Binary(int, int); "
        "int run() { Binary@ operation = function(left, right) { "
        "return left * right + left; }; return operation(6, 6); }");
    CHECK(module->Build());
    bool emittedClosure = false;
    for (const auto& function : module->Bytecode().functions)
        for (const auto& instruction : function.code)
            emittedClosure = emittedClosure || instruction.opcode == mini_as::OpCode::MakeClosure;
    CHECK(emittedClosure);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(captured_locals_are_shared_mutable_cells_that_outlive_their_scope) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("captured-locals");
    module->AddScriptSection("captured-locals",
        "funcdef int Step(int); "
        "Step@ makeCounter(int start) { int total = start; "
        "return function(value) { total += value; return total; }; } "
        "int run() { Step@ counter = makeCounter(10); "
        "return counter(5) * 100 + counter(7); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 1522);
}

TEST_CASE(captured_locals_share_updates_with_the_creating_scope) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("shared-captured-locals");
    module->AddScriptSection("shared-captured-locals",
        "funcdef int Step(int); int run() { int total = 1; "
        "Step@ step = function(value) { total += value; return total; }; "
        "total = 10; return step(2) + total; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 24);
}

TEST_CASE(anonymous_functions_reject_ambiguous_funcdef_inference) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("ambiguous-anonymous-function");
    module->AddScriptSection("ambiguous-anonymous-function",
        "funcdef int IntStep(int); funcdef float FloatStep(float); "
        "int run() { auto callback = function(value) { return value; }; return 0; }");
    CHECK(!module->Build());
    bool ambiguous = false;
    for (const auto& diagnostic : diagnostics)
        ambiguous = ambiguous || diagnostic.message.find("signature is ambiguous") != std::string::npos;
    CHECK(ambiguous);
}

TEST_CASE(anonymous_function_exceptions_retain_body_and_call_locations) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("anonymous-function-exception");
    module->AddScriptSection("anonymous-function-exception",
        "funcdef int Unary(int);\n"
        "int run() {\n"
        "  Unary@ callback = function(value) {\n"
        "    return 10 / value;\n"
        "  };\n"
        "  return callback(0);\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "division by zero");
    CHECK(context->GetExceptionLocation().row == 4);
    CHECK(context->GetCallStack().size() == 2);
    CHECK(context->GetCallStack()[1].location.row == 6);
}

TEST_CASE(child_funcdefs_bind_function_handles_inside_and_outside_the_parent_class) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("child-funcdefs");
    module->AddScriptSection("child-funcdefs",
        "class Dispatcher { funcdef int Callback(int value); Callback@ callback; "
        "int invoke(int value) { return callback(value); } } "
        "class DerivedDispatcher : Dispatcher { Callback@ derivedCallback; "
        "int invokeDerived(int value) { return derivedCallback(value); } } "
        "int twice(int value) { return value * 2; } "
        "int run() { DerivedDispatcher@ box = DerivedDispatcher(); "
        "Dispatcher::Callback@ callback = @twice; "
        "@box.derivedCallback = @callback; return box.invokeDerived(21); }");
    CHECK(module->Build());
    CHECK(module->Bytecode().funcdefs.size() == 1);
    CHECK(module->Bytecode().funcdefs[0].name == "Dispatcher::Callback");
    CHECK(module->Bytecode().funcdefs[0].parentType == "Dispatcher");
    CHECK(module->Bytecode().funcdefs[0].id.IsValid());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(child_funcdefs_reject_duplicate_and_cross_parent_handle_types) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("bad-child-funcdefs");
    module->AddScriptSection("bad-child-funcdefs",
        "class First { funcdef int Callback(int); funcdef int Callback(int); } "
        "class Second { funcdef int Callback(int); } "
        "int identity(int value) { return value; } "
        "int run() { First::Callback@ first = @identity; "
        "Second::Callback@ second = @first; return 0; }");
    CHECK(!module->Build());
    bool duplicate = false;
    bool incompatible = false;
    for (const auto& diagnostic : diagnostics) {
        duplicate = duplicate ||
            diagnostic.message.find("duplicate or conflicting funcdef") != std::string::npos;
        incompatible = incompatible ||
            diagnostic.message.find("cannot initialize Second::Callback@") != std::string::npos;
    }
    CHECK(duplicate);
    CHECK(incompatible);
}

TEST_CASE(child_funcdefs_are_rejected_in_interfaces) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("interface-child-funcdef");
    module->AddScriptSection("interface-child-funcdef",
        "interface Invalid { funcdef void Callback(); }");
    CHECK(!module->Build());
    bool rejected = false;
    for (const auto& diagnostic : diagnostics)
        rejected = rejected ||
            diagnostic.message.find("interfaces cannot declare child funcdefs") != std::string::npos;
    CHECK(rejected);
}

TEST_CASE(null_child_funcdef_handles_report_the_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("null-child-funcdef");
    module->AddScriptSection("null-child-funcdef",
        "class Dispatcher { funcdef int Callback(int); }\n"
        "int run() {\n"
        "  Dispatcher::Callback@ callback;\n"
        "  return callback(42);\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("null function handle") != std::string::npos);
    CHECK(context->GetExceptionLocation().row == 4);
}

TEST_CASE(weak_references_lock_live_objects_and_expire_without_owning_them) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("weak-references");
    module->AddScriptSection("weak-references",
        "class Payload { int value; Payload(int input) { value = input; } } "
        "weakref<Payload> reference; "
        "int run() { Payload@ object = Payload(40); @reference = object; "
        "const_weakref<Payload> readonly = reference; Payload@ locked = readonly; "
        "int result = locked.value; "
        "@object = null; if (reference.get() is null) return 0; "
        "@locked = null; return reference.get() is null ? result + 2 : 0; }");
    CHECK(module->Build());
    bool madeWeak = false, lockedWeak = false, madeConst = false;
    for (const auto& function : module->Bytecode().functions) {
        for (const auto& instruction : function.code) {
            madeWeak = madeWeak || instruction.opcode == mini_as::OpCode::MakeWeakRef;
            lockedWeak = lockedWeak || instruction.opcode == mini_as::OpCode::LockWeakRef;
            madeConst = madeConst || instruction.opcode == mini_as::OpCode::ToConstWeakRef;
        }
    }
    CHECK(madeWeak);
    CHECK(lockedWeak);
    CHECK(madeConst);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(engine->GetTrackedObjectCount() == 0);
}

TEST_CASE(weak_references_reject_invalid_subtypes_and_cross_type_assignment) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* invalid = engine->GetModule("invalid-weakref");
    invalid->AddScriptSection("invalid-weakref",
        "class Payload {} weakref<Payload@> badHandle; weakref<int> badValue;");
    CHECK(!invalid->Build());
    auto* mismatch = engine->GetModule("mismatched-weakref");
    mismatch->AddScriptSection("mismatched-weakref",
        "class First {} class Second {} int run() { First@ first = First(); "
        "weakref<Second> reference; @reference = first; return 0; }");
    CHECK(!mismatch->Build());
    bool subtype = false, assignment = false;
    for (const auto& diagnostic : diagnostics) {
        subtype = subtype || diagnostic.message.find("weakref subtype must be a script class") !=
            std::string::npos || diagnostic.message.find("without '@'") != std::string::npos;
        assignment = assignment || diagnostic.message.find("cannot assign First@") !=
            std::string::npos;
    }
    CHECK(subtype);
    CHECK(assignment);
}

TEST_CASE(generated_copy_constructors_copy_inherited_values_and_share_handles) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("generated-copy-constructors");
    module->AddScriptSection("generated-copy-constructors",
        "int constructions = 0; class Payload { int value; } "
        "class Base { int baseValue; Base() { constructions += 1; } } "
        "class Derived : Base { int ownValue; Payload@ payload; } "
        "int run() { Derived@ original = Derived(); original.baseValue = 3; "
        "original.ownValue = 4; @original.payload = Payload(); original.payload.value = 5; "
        "Derived@ copied = Derived(original); original.baseValue = 30; "
        "original.ownValue = 40; original.payload.value = 50; "
        "return copied.baseValue * 100 + copied.ownValue * 10 + copied.payload.value + constructions; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 391);
}

TEST_CASE(single_argument_constructors_suppress_generated_copy_constructors) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("suppressed-generated-copy");
    module->AddScriptSection("suppressed-generated-copy",
        "class Box { Box(int value) {} } int run() { Box@ source = Box(1); "
        "Box@ copied = Box(source); return 0; }");
    CHECK(!module->Build());
    bool missing = false;
    for (const auto& diagnostic : diagnostics)
        missing = missing || diagnostic.message.find("no matching constructor for 'Box'") !=
            std::string::npos;
    CHECK(missing);
}

TEST_CASE(null_generated_copy_sources_report_the_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("null-generated-copy");
    module->AddScriptSection("null-generated-copy",
        "class Box { int value; }\n"
        "int run() {\n"
        "  Box@ source;\n"
        "  Box@ copied = Box(source);\n"
        "  return copied.value;\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("copy constructor source is null") != std::string::npos);
    CHECK(context->GetExceptionLocation().row == 4);
}

TEST_CASE(deleted_default_operations_allow_alternate_construction_and_handle_rebinding) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("deleted-default-operations");
    module->AddScriptSection("deleted-default-operations",
        "class NoDefault { int value; NoDefault() delete; NoDefault(int input) { value = input; } } "
        "class NoCopy { NoCopy() {} NoCopy(const NoCopy &in other) delete; } "
        "class NoAssign { NoAssign &opAssign(const NoAssign &in other) delete; } "
        "int run() { NoDefault@ configured = NoDefault(42); NoCopy@ guarded = NoCopy(); "
        "NoAssign@ left = NoAssign(); NoAssign@ right = NoAssign(); @left = right; "
        "return guarded is null || !(left is right) ? 0 : configured.value; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(deleted_default_operations_reject_implicit_uses) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("deleted-operation-uses");
    module->AddScriptSection("deleted-operation-uses",
        "class NoDefault { NoDefault() delete; } "
        "class NoCopy { NoCopy() {} NoCopy(const NoCopy &in other) delete; } "
        "class NoAssign { NoAssign &opAssign(const NoAssign &in other) delete; } "
        "class Base { Base() delete; Base(int value) {} } class Derived : Base {} "
        "int badDefault() { NoDefault@ value = NoDefault(); return 0; } "
        "int badCopy() { NoCopy@ source = NoCopy(); NoCopy@ copied = NoCopy(source); return 0; } "
        "int badAssign() { NoAssign@ left = NoAssign(); NoAssign@ right = NoAssign(); "
        "left = right; return 0; }");
    CHECK(!module->Build());
    bool defaultConstructor = false, copyConstructor = false, copyAssignment = false;
    bool deletedBaseConstructor = false;
    for (const auto& diagnostic : diagnostics) {
        defaultConstructor = defaultConstructor ||
            diagnostic.message.find("default constructor for 'NoDefault' is deleted") != std::string::npos;
        copyConstructor = copyConstructor ||
            diagnostic.message.find("no matching constructor for 'NoCopy'") != std::string::npos;
        copyAssignment = copyAssignment ||
            diagnostic.message.find("copy assignment for 'NoAssign' is deleted") != std::string::npos;
        deletedBaseConstructor = deletedBaseConstructor ||
            diagnostic.message.find("default constructor for base class 'Base' is deleted") !=
                std::string::npos;
    }
    CHECK(defaultConstructor);
    CHECK(copyConstructor);
    CHECK(copyAssignment);
    CHECK(deletedBaseConstructor);
}

TEST_CASE(delete_rejects_non_default_operations_implementations_and_conflicts) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("invalid-deleted-operations");
    module->AddScriptSection("invalid-deleted-operations",
        "class Invalid { Invalid(int value) delete; void method() delete; "
        "Invalid() delete {} } "
        "class Conflict { Conflict() delete; Conflict() {} } "
        "interface Contract { Contract &opAssign(const Contract &in other) delete; } "
        "void removed() delete;");
    CHECK(!module->Build());
    bool nonDefault = false, implementation = false, conflict = false;
    bool interfaceMethod = false, globalFunction = false;
    for (const auto& diagnostic : diagnostics) {
        nonDefault = nonDefault || diagnostic.message.find("only default construction") != std::string::npos;
        implementation = implementation ||
            diagnostic.message.find("deleted function cannot have an implementation") != std::string::npos;
        conflict = conflict ||
            diagnostic.message.find("cannot define a default constructor that is deleted") != std::string::npos;
        interfaceMethod = interfaceMethod ||
            diagnostic.message.find("interface methods cannot be deleted") != std::string::npos;
        globalFunction = globalFunction ||
            diagnostic.message.find("only class default operations can be deleted") != std::string::npos;
    }
    CHECK(nonDefault);
    CHECK(implementation);
    CHECK(conflict);
    CHECK(interfaceMethod);
    CHECK(globalFunction);
}

