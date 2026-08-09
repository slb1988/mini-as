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

