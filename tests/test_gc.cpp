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

TEST_CASE(gc_cycle_detection_advances_with_a_bounded_work_budget) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("gc-incremental");
    module->AddScriptSection("gc-incremental",
        "class Node { Node@ next; } "
        "Node@ makeCycle() { Node@ a = Node(); Node@ b = Node(); "
        "a.next = b; b.next = a; return a; }");
    CHECK(module->Build());
    auto root = BuildCycle(*engine, *module);
    root = {};

    CHECK(engine->CollectGarbageStep(0) == 0);
    CHECK(!engine->IsGarbageCollectionInProgress());
    CHECK(engine->CollectGarbageStep(1) == 0);
    CHECK(engine->IsGarbageCollectionInProgress());
    CHECK(engine->GetTrackedObjectCount() == 2);

    std::size_t collected = 0;
    std::size_t steps = 1;
    while (engine->GetTrackedObjectCount() != 0 && steps < 32) {
        collected += engine->CollectGarbageStep(1);
        ++steps;
    }
    CHECK(steps >= 6);
    CHECK(collected == 2);
    CHECK(engine->GetTrackedObjectCount() == 0);
    CHECK(!engine->IsGarbageCollectionInProgress());
}

TEST_CASE(gc_incremental_cycle_restarts_when_reference_counts_change) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("gc-incremental-mutation");
    module->AddScriptSection("gc-incremental-mutation",
        "class Node { Node@ next; } "
        "Node@ makeCycle() { Node@ a = Node(); Node@ b = Node(); "
        "a.next = b; b.next = a; return a; }");
    CHECK(module->Build());
    auto root = BuildCycle(*engine, *module);
    mini_as::WeakObjectHandle observer(root, "Node", false);
    root = {};
    CHECK(engine->CollectGarbageStep(1) == 0);
    CHECK(engine->IsGarbageCollectionInProgress());

    auto rescued = observer.Lock();
    CHECK(rescued);
    std::size_t collected = 0;
    for (int step = 0; step < 16; ++step)
        collected += engine->CollectGarbageStep(1);
    CHECK(collected == 0);
    CHECK(engine->GetTrackedObjectCount() == 2);

    rescued = {};
    for (int step = 0; step < 32 && engine->GetTrackedObjectCount() != 0; ++step)
        collected += engine->CollectGarbageStep(1);
    CHECK(collected == 2);
    CHECK(engine->GetTrackedObjectCount() == 0);
}

TEST_CASE(gc_exposes_lifetime_statistics_and_detected_cycle_objects) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("gc-statistics");
    module->AddScriptSection("gc-statistics",
        "class Node { Node@ next; } "
        "void makeTransient() { Node@ value = Node(); } "
        "Node@ makeCycle() { Node@ a = Node(); Node@ b = Node(); "
        "a.next = b; b.next = a; return a; }");
    CHECK(module->Build());
    auto initial = engine->GetGarbageCollectionStatistics();
    CHECK(initial.currentSize == 0);
    CHECK(initial.totalDestroyed == 0);

    auto transient = engine->CreateContext();
    CHECK(transient->Prepare(module->GetFunctionByDecl("void makeTransient()")));
    CHECK(transient->Execute() == mini_as::ExecutionState::Finished);
    transient.reset();
    auto afterTransient = engine->GetGarbageCollectionStatistics();
    CHECK(afterTransient.currentSize == 0);
    CHECK(afterTransient.totalDestroyed == 1);
    CHECK(afterTransient.totalNewDestroyed == 1);

    auto root = BuildCycle(*engine, *module);
    auto beforeCycle = engine->GetGarbageCollectionStatistics();
    CHECK(beforeCycle.currentSize == 2);
    CHECK(beforeCycle.newObjects == 2);
    root = {};
    std::size_t callbacks = 0;
    engine->SetCircularReferenceDetectedCallback(
        [&](const mini_as::TypeInfo* type, const mini_as::RefObject* object) {
            CHECK(type != nullptr);
            CHECK(type->name == "Node");
            const auto* script = dynamic_cast<const mini_as::ScriptObject*>(object);
            CHECK(script != nullptr);
            CHECK(script->FieldCount() == 1);
            ++callbacks;
        });
    CHECK(engine->CollectGarbage() == 2);
    CHECK(callbacks == 2);
    const auto afterCycle = engine->GetGarbageCollectionStatistics();
    CHECK(afterCycle.currentSize == 0);
    CHECK(afterCycle.totalDestroyed == 3);
    CHECK(afterCycle.totalDetected == 2);
    CHECK(afterCycle.newObjects == 0);
    CHECK(afterCycle.totalNewDestroyed == 1);
}
