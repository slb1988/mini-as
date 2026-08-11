#include "test.hpp"
#include "mini_as/engine.hpp"

namespace {
struct ManagedLink {
    mini_as::ObjectHandle object;
};

mini_as::Value MakeManagedLink(mini_as::ObjectHandle object = {}) {
    return mini_as::Value::ManagedHostValue<ManagedLink>(
        "ManagedLink", ManagedLink{std::move(object)},
        [](const ManagedLink& link, const mini_as::ReferenceVisitor& visitor) {
            if (link.object) visitor(link.object.Get());
        },
        [](ManagedLink& link) { link.object = {}; });
}

mini_as::ObjectHandle BuildCycle(mini_as::ScriptEngine& engine, mini_as::ScriptModule& module) {
    auto context = engine.CreateContext();
    if (!context->Prepare(module.GetFunctionByName("makeCycle"))) throw std::runtime_error("prepare failed");
    if (context->Execute() != mini_as::ExecutionState::Finished) throw std::runtime_error(context->GetExceptionString());
    return context->GetReturnValue().As<mini_as::ObjectHandle>();
}

TEST_CASE(gc_traverses_references_owned_by_registered_host_values) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterValueType("ManagedLink", MakeManagedLink()) != nullptr);
    auto* module = engine->GetModule("gc-managed-host-value");
    module->AddScriptSection("gc-managed-host-value",
        "class Node { ManagedLink link; } Node@ make() { return Node(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("Node@ make()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    mini_as::ObjectHandle root = context->GetReturnValue().As<mini_as::ObjectHandle>();
    context.reset();
    auto* object = dynamic_cast<mini_as::ScriptObject*>(root.Get());
    CHECK(object != nullptr);
    object->SetField(0, MakeManagedLink(root));
    root = {};
    CHECK(engine->GetTrackedObjectCount() == 1);
    CHECK(engine->CollectGarbage() == 1);
    CHECK(engine->GetTrackedObjectCount() == 0);
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

TEST_CASE(gc_collects_cycles_formed_by_delegate_receivers) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("gc-delegate-cycle");
    module->AddScriptSection("gc-delegate-cycle",
        "funcdef int Unary(int); "
        "class Node { Unary@ callback; int ping(int value) { return value; } } "
        "Node@ makeCycle() { Node@ node = Node(); @node.callback = Unary(node.ping); return node; }");
    CHECK(module->Build());
    mini_as::ObjectHandle root = BuildCycle(*engine, *module);
    CHECK(engine->CollectGarbage() == 0);
    root = {};
    CHECK(engine->CollectGarbage() == 1);
    CHECK(engine->GetTrackedObjectCount() == 0);
}

TEST_CASE(gc_collects_cycles_formed_by_captured_object_handles) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("gc-closure-cycle");
    module->AddScriptSection("gc-closure-cycle",
        "funcdef int Step(int); "
        "class Node { Step@ callback; int ping(int value) { return value; } } "
        "Node@ makeCycle() { Node@ node = Node(); @node.callback = "
        "function(value) { return node.ping(value); }; return node; }");
    CHECK(module->Build());
    mini_as::ObjectHandle root = BuildCycle(*engine, *module);
    CHECK(engine->CollectGarbage() == 0);
    root = {};
    CHECK(engine->CollectGarbage() == 1);
    CHECK(engine->GetTrackedObjectCount() == 0);
}

TEST_CASE(weakref_fields_do_not_create_gc_edges_or_keep_objects_alive) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("gc-weakref");
    module->AddScriptSection("gc-weakref",
        "class Node { weakref<Node> self; } weakref<Node> observer; "
        "void make() { Node@ node = Node(); @node.self = node; @observer = node; } "
        "int run() { make(); return observer.get() is null ? 42 : 0; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("run")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(engine->GetTrackedObjectCount() == 0);
    CHECK(engine->CollectGarbage() == 0);
}
