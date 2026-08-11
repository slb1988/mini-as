#include "test.hpp"
#include "mini_as/coroutine.hpp"

#include <memory>
#include <string>
#include <vector>

TEST_CASE(coroutine_scheduler_runs_yielding_scripts_fairly) {
    auto engine = mini_as::CreateScriptEngine();
    std::string trace;
    CHECK(engine->RegisterGlobalFunction("void mark(string value)",
        [&](mini_as::GenericCall& call) { trace += call.GetArgString(0); }));
    mini_as::CoroutineScheduler scheduler(*engine);
    CHECK(scheduler.RegisterYieldFunction());

    auto* module = engine->GetModule("coroutine-fairness");
    module->AddScriptSection("coroutines.as",
        "int first() {\n"
        "  mark(\"a\"); yield();\n"
        "  mark(\"c\"); yield();\n"
        "  mark(\"e\"); return 42;\n"
        "}\n"
        "int second() {\n"
        "  mark(\"b\"); yield();\n"
        "  mark(\"d\"); return 7;\n"
        "}\n");
    CHECK(module->Build());
    bool hasStatementCue = false;
    bool callsYieldHost = false;
    for (const auto& instruction : module->GetFunctionByDecl("int first()")->code) {
        hasStatementCue = hasStatementCue ||
            instruction.opcode == mini_as::OpCode::Suspend;
        callsYieldHost = callsYieldHost ||
            instruction.opcode == mini_as::OpCode::CallHost;
    }
    CHECK(hasStatementCue);
    CHECK(callsYieldHost);
    const auto first = scheduler.Start(module->GetFunctionByDecl("int first()"));
    const auto second = scheduler.Start(module->GetFunctionByDecl("int second()"));
    CHECK(first != 0);
    CHECK(second != 0);
    CHECK(first != second);
    CHECK(scheduler.GetCoroutineCount() == 2);

    CHECK(scheduler.ExecuteRound() == 2);
    CHECK(trace == "ab");
    CHECK(scheduler.ExecuteRound() == 1);
    CHECK(trace == "abcd");
    CHECK(scheduler.ExecuteRound() == 0);
    CHECK(trace == "abcde");
    CHECK(scheduler.GetCurrentCoroutine() == 0);

    const auto firstResult = scheduler.TakeCompleted(first);
    const auto secondResult = scheduler.TakeCompleted(second);
    CHECK(firstResult.has_value());
    CHECK(secondResult.has_value());
    CHECK(firstResult->state == mini_as::ExecutionState::Finished);
    CHECK(firstResult->returnValue.As<std::int32_t>() == 42);
    CHECK(secondResult->state == mini_as::ExecutionState::Finished);
    CHECK(secondResult->returnValue.As<std::int32_t>() == 7);
    CHECK(scheduler.GetCompleted().empty());
}

TEST_CASE(coroutine_scheduler_preserves_exception_location_after_yield) {
    auto engine = mini_as::CreateScriptEngine();
    mini_as::CoroutineScheduler scheduler(*engine);
    CHECK(scheduler.RegisterYieldFunction());
    auto* module = engine->GetModule("coroutine-exception");
    module->AddScriptSection("failure.as",
        "int fail() {\n"
        "  yield();\n"
        "  int zero = 0;\n"
        "  return 1 / zero;\n"
        "}\n");
    CHECK(module->Build());
    const auto id = scheduler.Start(module->GetFunctionByDecl("int fail()"));
    CHECK(id != 0);
    CHECK(scheduler.ExecuteRound() == 1);
    CHECK(scheduler.ExecuteRound() == 0);
    const auto result = scheduler.TakeCompleted(id);
    CHECK(result.has_value());
    CHECK(result->state == mini_as::ExecutionState::Exception);
    CHECK(result->exception == "division by zero");
    CHECK(result->location.section == "failure.as");
    CHECK(result->location.row == 4);
    CHECK(result->callStack.size() == 1);
}

TEST_CASE(coroutine_scheduler_validates_start_and_supports_abort) {
    auto engine = mini_as::CreateScriptEngine();
    mini_as::CoroutineScheduler scheduler(*engine);
    CHECK(scheduler.RegisterYieldFunction());
    auto* module = engine->GetModule("coroutine-abort");
    module->AddScriptSection("abort.as",
        "void wait(int value) { while (value > 0) { yield(); value--; } }\n");
    CHECK(module->Build());
    const auto* function = module->GetFunctionByDecl("void wait(int)");
    CHECK(scheduler.Start(nullptr) == 0);
    CHECK(scheduler.Start(function) == 0);
    CHECK(scheduler.Start(function, {mini_as::Value(std::string("wrong"))}) == 0);
    const auto first = scheduler.Start(function, {mini_as::Value(2)});
    const auto second = scheduler.Start(function, {mini_as::Value(3)});
    CHECK(first != 0);
    CHECK(second != 0);
    CHECK(scheduler.ExecuteRound() == 2);
    CHECK(scheduler.Abort(first));
    CHECK(!scheduler.Abort(first));
    scheduler.AbortAll();
    CHECK(scheduler.GetCoroutineCount() == 0);
    const auto firstResult = scheduler.TakeCompleted(first);
    const auto secondResult = scheduler.TakeCompleted(second);
    CHECK(firstResult->state == mini_as::ExecutionState::Aborted);
    CHECK(secondResult->state == mini_as::ExecutionState::Aborted);
}

TEST_CASE(coroutine_yield_outside_its_scheduler_is_a_located_exception) {
    auto engine = mini_as::CreateScriptEngine();
    {
        mini_as::CoroutineScheduler scheduler(*engine);
        CHECK(scheduler.RegisterYieldFunction());
    }
    auto* module = engine->GetModule("orphaned-yield");
    module->AddScriptSection("orphan.as",
        "int run() {\n"
        "  yield();\n"
        "  return 42;\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() ==
          "yield() requires an active coroutine");
    CHECK(context->GetExceptionLocation().section == "orphan.as");
    CHECK(context->GetExceptionLocation().row == 2);
}

TEST_CASE(coroutine_can_request_its_own_abort_without_reentering_the_vm) {
    auto engine = mini_as::CreateScriptEngine();
    mini_as::CoroutineScheduler scheduler(*engine);
    CHECK(engine->RegisterGlobalFunction("void cancelSelf()",
        [&](mini_as::GenericCall& call) {
            const auto current = scheduler.GetCurrentCoroutine();
            if (current == 0 || !scheduler.Abort(current))
                call.SetException("no active coroutine to cancel");
        }));
    auto* module = engine->GetModule("self-cancel");
    module->AddScriptSection("self-cancel.as",
        "int run() { cancelSelf(); return 42; }");
    CHECK(module->Build());
    const auto id = scheduler.Start(module->GetFunctionByDecl("int run()"));
    CHECK(id != 0);
    CHECK(scheduler.ExecuteRound() == 0);
    const auto result = scheduler.TakeCompleted(id);
    CHECK(result.has_value());
    CHECK(result->state == mini_as::ExecutionState::Aborted);
}

TEST_CASE(coroutine_scheduler_returns_completed_contexts_to_the_engine_pool) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<std::unique_ptr<mini_as::ScriptContext>> pool;
    CHECK(engine->SetContextCallbacks(
        [&](mini_as::ScriptEngine& owner) -> mini_as::ScriptContext* {
            if (pool.empty()) return owner.CreateContext().release();
            auto context = std::move(pool.back());
            pool.pop_back();
            return context.release();
        },
        [&](mini_as::ScriptEngine&, mini_as::ScriptContext* context) {
            CHECK(context->Unprepare());
            pool.emplace_back(context);
        }));
    mini_as::CoroutineScheduler scheduler(*engine);
    CHECK(scheduler.RegisterYieldFunction());
    auto* module = engine->GetModule("pooled-coroutine");
    module->AddScriptSection("pooled.as", "int run() { yield(); return 42; }");
    CHECK(module->Build());
    const auto id = scheduler.Start(module->GetFunctionByDecl("int run()"));
    CHECK(id != 0);
    CHECK(scheduler.ExecuteRound() == 1);
    CHECK(pool.empty());
    CHECK(scheduler.ExecuteRound() == 0);
    CHECK(pool.size() == 1);
    const auto result = scheduler.TakeCompleted(id);
    CHECK(result->returnValue.As<std::int32_t>() == 42);
}
