#include "test.hpp"
#include "mini_as/engine.hpp"

namespace {
mini_as::ObjectHandle BuildCycle(mini_as::ScriptEngine& engine, mini_as::ScriptModule& module) {
    auto context = engine.CreateContext();
    if (!context->Prepare(module.GetFunctionByName("makeCycle"))) throw std::runtime_error("prepare failed");
    if (context->Execute() != mini_as::ExecutionState::Finished) throw std::runtime_error(context->GetExceptionString());
    return context->GetReturnValue().As<mini_as::ObjectHandle>();
}
}

TEST_CASE(gc_collects_unreachable_two_object_cycle) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("gc-cycle");
    module->AddScriptSection("gc-cycle",
        "class Node { Node@ next; }"
        "Node@ makeCycle() { Node@ a = Node(); Node@ b = Node(); a.next = b; b.next = a; return a; }");
    CHECK(module->Build());
    mini_as::ObjectHandle root = BuildCycle(*engine, *module);
    CHECK(engine->GetTrackedObjectCount() == 2);
    CHECK(engine->CollectGarbage() == 0);
    root = {};
    CHECK(engine->CollectGarbage() == 2);
    CHECK(engine->GetTrackedObjectCount() == 0);
}

TEST_CASE(gc_collects_self_cycle_but_keeps_external_roots) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("gc-self");
    module->AddScriptSection("gc-self",
        "class Node { Node@ next; }"
        "Node@ makeCycle() { Node@ a = Node(); a.next = a; return a; }");
    CHECK(module->Build());
    mini_as::ObjectHandle root = BuildCycle(*engine, *module);
    CHECK(engine->CollectGarbage() == 0);
    root = {};
    CHECK(engine->CollectGarbage() == 1);
    CHECK(engine->GetTrackedObjectCount() == 0);
}

TEST_CASE(gc_does_not_collect_graph_reachable_from_external_handle) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("gc-root");
    module->AddScriptSection("gc-root",
        "class Node { Node@ next; }"
        "Node@ makeCycle() { Node@ a = Node(); Node@ b = Node(); a.next = b; b.next = a; return b; }");
    CHECK(module->Build());
    auto root = BuildCycle(*engine, *module);
    CHECK(engine->CollectGarbage() == 0);
    CHECK(engine->GetTrackedObjectCount() == 2);
    root = {};
    CHECK(engine->CollectGarbage() == 2);
}

TEST_CASE(gc_queues_each_script_destructor_once_for_cycles) {
    auto engine = mini_as::CreateScriptEngine();
    int finalized = 0;
    CHECK(engine->RegisterGlobalFunction("void Finalized()",
        [&](mini_as::GenericCall&) { ++finalized; }));
    auto* module = engine->GetModule("gc-destructors");
    module->AddScriptSection("gc-destructors",
        "class Node { Node@ next; ~Node() { Finalized(); } } "
        "Node@ makeCycle() { Node@ a = Node(); Node@ b = Node(); "
        "a.next = b; b.next = a; return a; }");
    CHECK(module->Build());
    mini_as::ObjectHandle root = BuildCycle(*engine, *module);
    root = {};
    CHECK(engine->CollectGarbage() == 2);
    CHECK(finalized == 2);
    CHECK(engine->CollectGarbage() == 0);
    CHECK(finalized == 2);
}
