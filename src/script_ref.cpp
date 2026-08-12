#include "mini_as/addons/ref.hpp"

#include <stdexcept>
#include <utility>

namespace mini_as::addons {

Value MakeScriptRef(ObjectHandle object) {
    return Value::ManagedHostValue<ScriptRefValue>(
        "ref", ScriptRefValue{std::move(object)},
        [](const ScriptRefValue& value, const ReferenceVisitor& visitor) {
            if (value.object) visitor(value.object.Get());
        },
        [](ScriptRefValue& value) { value.object = {}; });
}

const ObjectHandle& GetScriptRef(const Value& value) {
    return value.AsHostValue<ScriptRefValue>().object;
}

bool RegisterScriptRef(ScriptEngine& engine) {
    if (!engine.RegisterValueType("ref", MakeScriptRef())) return false;
    bool ok = engine.RegisterObjectMethod("ref", "bool opEquals(ref other) const",
        [](GenericCall& call) {
            call.SetReturnBool(GetScriptRef(call.GetObjectValue()) == GetScriptRef(call.GetArg(0)));
        });
    ok = ok && engine.RegisterObjectMethod("ref", "bool isNull() const",
        [](GenericCall& call) {
            call.SetReturnBool(!GetScriptRef(call.GetObjectValue()));
        });
    ok = ok && engine.RegisterObjectMethod("ref", "string typeName() const",
        [](GenericCall& call) {
            const auto& object = GetScriptRef(call.GetObjectValue());
            call.SetReturnString(object && object.Get()->GetTypeInfo()
                ? object.Get()->GetTypeInfo()->name : std::string{});
        });
    return ok;
}

} // namespace mini_as::addons
