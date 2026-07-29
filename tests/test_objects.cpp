#include "test.hpp"
#include "mini_as/engine.hpp"

namespace {
class HostThing final : public mini_as::RefObject {
public:
    HostThing(const mini_as::TypeInfo* type, int value, int& destroyed)
        : RefObject(type), value(value), destroyed_(destroyed) {}
    int value;
private:
    ~HostThing() override { ++destroyed_; }
    int& destroyed_;
};
}

TEST_CASE(object_handles_addref_release_deterministically) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    {
        mini_as::ObjectHandle first(new HostThing(type, 7, destroyed));
        CHECK(first.Get()->RefCount() == 1);
        { mini_as::ObjectHandle second = first; CHECK(first.Get()->RefCount() == 2); }
        CHECK(first.Get()->RefCount() == 1);
    }
    CHECK(destroyed == 1);
}

TEST_CASE(object_handles_round_trip_through_script_and_generic_bridge) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    int destroyed = 0;
    CHECK(engine->RegisterGlobalFunction("Thing@ Identity(Thing@ value)", [](mini_as::GenericCall& call) {
        call.SetReturnObject(call.GetArgObject(0));
    }));
    auto* module = engine->GetModule("objects");
    module->AddScriptSection("objects", "Thing@ pass(Thing@ value) { return Identity(value); }");
    CHECK(module->Build());
    mini_as::ObjectHandle object(new HostThing(type, 9, destroyed));
    {
        auto context = engine->CreateContext();
        CHECK(context->Prepare(module->GetFunctionByName("pass")));
        CHECK(context->SetArgObject(0, object));
        CHECK(context->Execute() == mini_as::ExecutionState::Finished);
        auto returned = context->GetReturnValue().As<mini_as::ObjectHandle>();
        CHECK(returned.Get() == object.Get());
    }
    CHECK(destroyed == 0);
    object = {};
    CHECK(destroyed == 1);
}

TEST_CASE(context_rejects_wrong_object_type) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* thing = engine->RegisterObjectType("Thing");
    const auto* other = engine->RegisterObjectType("Other");
    int destroyed = 0;
    auto* module = engine->GetModule("object-types");
    module->AddScriptSection("types", "Thing@ pass(Thing@ value) { return value; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("pass")));
    mini_as::ObjectHandle wrong(new HostThing(other, 0, destroyed));
    CHECK(!context->SetArgObject(0, wrong));
    (void)thing;
}

TEST_CASE(script_classes_allocate_fields_and_cross_context_boundaries) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("script-objects");
    module->AddScriptSection("classes",
        "class Box { int value; string label; }"
        "Box@ makeBox(int value) { Box@ box = Box(); box.value = value; box.label = \"answer\"; return box; }"
        "int readBox(Box@ box) { return box.value; }");
    CHECK(module->Build());
    auto make = engine->CreateContext();
    CHECK(make->Prepare(module->GetFunctionByName("makeBox")));
    CHECK(make->SetArgInt(0, 42));
    CHECK(make->Execute() == mini_as::ExecutionState::Finished);
    mini_as::ObjectHandle box = make->GetReturnValue().As<mini_as::ObjectHandle>();
    auto* scriptBox = dynamic_cast<mini_as::ScriptObject*>(box.Get());
    CHECK(scriptBox != nullptr);
    CHECK(scriptBox->FieldCount() == 2);
    CHECK(scriptBox->GetField(1).As<std::string>() == "answer");

    auto read = engine->CreateContext();
    CHECK(read->Prepare(module->GetFunctionByName("readBox")));
    CHECK(read->SetArgObject(0, box));
    CHECK(read->Execute() == mini_as::ExecutionState::Finished);
    CHECK(read->GetReturnInt() == 42);
}

TEST_CASE(script_class_interface_table_is_validated_and_resolvable) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("interfaces");
    module->AddScriptSection("interfaces",
        "interface IValue { int get(); }"
        "class Box : IValue { int value; int get() { return value; } }"
        "Box@ make() { return Box(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("make")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    auto box = context->GetReturnValue().As<mini_as::ObjectHandle>();
    auto* object = dynamic_cast<mini_as::ScriptObject*>(box.Get());
    CHECK(object != nullptr);
    CHECK(object->Implements("IValue"));
    CHECK(object->ResolveInterfaceMethod("IValue", "int get()") == "Box::int get()");
}

TEST_CASE(script_class_missing_interface_method_is_compile_error) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& message) { diagnostics.push_back(message); });
    auto* module = engine->GetModule("bad-interface");
    module->AddScriptSection("bad-interface",
        "interface IValue { int get(); } class Empty : IValue { int value; }");
    CHECK(!module->Build());
    CHECK(!diagnostics.empty());
}

