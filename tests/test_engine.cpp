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

