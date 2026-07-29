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

