#include "mini_as/addons/any.hpp"
#include "mini_as/addons/ref.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mini_as::addons {
namespace {

ScriptAny* RequireAny(GenericCall& call) {
    auto* value = dynamic_cast<ScriptAny*>(call.GetObject().Get());
    if (!value) throw std::runtime_error("invalid any receiver");
    return value;
}

bool ConvertStoredValue(const Value& source, const DataType& target, Value& result) {
    if (source.Type() == target) {
        result = source;
        return true;
    }
    if (source.Type().IsInteger() && target.IsInteger()) {
        result = ConvertInteger(source, target);
        return true;
    }
    if (target.IsInteger() &&
        (source.Type() == DataType::Float() || source.Type() == DataType::Double())) {
        const double numeric = source.Type() == DataType::Float()
            ? static_cast<double>(source.As<float>()) : source.As<double>();
        const long double wide = static_cast<long double>(numeric);
        if (!std::isfinite(numeric) ||
            wide < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
            wide > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) return false;
        result = Value::Integer(target,
            static_cast<std::uint64_t>(static_cast<std::int64_t>(numeric)));
        return true;
    }
    if (target == DataType::Double() && source.Type().IsNumeric()) {
        if (source.Type() == DataType::Float()) result = Value(static_cast<double>(source.As<float>()));
        else if (source.Type().IsSignedInteger()) result = Value(static_cast<double>(source.SignedInteger()));
        else if (source.Type().IsUnsignedInteger()) result = Value(static_cast<double>(source.UnsignedInteger()));
        else result = source;
        return true;
    }
    return false;
}

void RegisterStoreOverload(ScriptEngine& engine, std::string_view typeName) {
    if (!engine.RegisterObjectMethod("any", "void store(" + std::string(typeName) + " value)",
        [](GenericCall& call) { RequireAny(call)->Store(call.GetArg(0)); }))
        throw std::runtime_error("any store overload registration failed");
}

void RegisterRetrieveOverload(ScriptEngine& engine, const DataType& type) {
    if (!engine.RegisterObjectMethod("any",
        "bool retrieve(" + type.Name() + " &out value) const",
        [type](GenericCall& call) {
            Value stored;
            Value converted;
            const bool found = RequireAny(call)->Retrieve(stored) &&
                               ConvertStoredValue(stored, type, converted);
            if (found) call.SetArg(0, std::move(converted));
            call.SetReturnBool(found);
        })) throw std::runtime_error("any retrieve overload registration failed");
}

} // namespace

ScriptAny::ScriptAny(const TypeInfo* type, Value value)
    : RefObject(type), value_(std::move(value)) {
    if (type && type->collector) type->collector->Register(this);
}

void ScriptAny::Store(Value value) { value_ = std::move(value); }
bool ScriptAny::Retrieve(Value& value) const {
    if (!HasValue()) return false;
    value = value_;
    return true;
}
const Value& ScriptAny::StoredValue() const { return value_; }
bool ScriptAny::HasValue() const { return !value_.IsVoid(); }
void ScriptAny::Clear() { value_ = Value{}; }
void ScriptAny::EnumerateReferences(
    const std::function<void(RefObject*)>& visitor) const { value_.EnumerateReferences(visitor); }
void ScriptAny::ClearReferences() {
    value_.ClearReferences();
    Clear();
}

bool RegisterScriptAny(ScriptEngine& engine) {
    const TypeInfo* type = engine.RegisterObjectType("any", true);
    if (!type) return false;
    bool ok = engine.RegisterObjectFactory("any", "any@ f()",
        [type](GenericCall& call) {
            call.SetReturnObject(ObjectHandle(new ScriptAny(type)));
        });
    const auto registerFactory = [&engine, type](std::string_view typeName) {
        return engine.RegisterObjectFactory("any",
            "any@ f(" + std::string(typeName) + " value)",
            [type](GenericCall& call) {
                call.SetReturnObject(ObjectHandle(new ScriptAny(type, call.GetArg(0))));
            });
    };
    ok = ok && registerFactory("int64");
    ok = ok && registerFactory("double");
    ok = ok && registerFactory("string");
    ok = ok && registerFactory("bool");
    if (engine.GetTypeInfo("ref")) ok = ok && registerFactory("ref");

    try {
        RegisterStoreOverload(engine, "int64");
        RegisterStoreOverload(engine, "double");
        RegisterStoreOverload(engine, "string");
        RegisterStoreOverload(engine, "bool");
        RegisterRetrieveOverload(engine, DataType::Int64());
        RegisterRetrieveOverload(engine, DataType::Double());
        RegisterRetrieveOverload(engine, DataType::String());
        RegisterRetrieveOverload(engine, DataType::Bool());
        if (engine.GetTypeInfo("ref")) {
            RegisterStoreOverload(engine, "ref");
            RegisterRetrieveOverload(engine, DataType::Object("ref", false));
        }
    } catch (const std::exception&) {
        return false;
    }
    ok = ok && engine.RegisterObjectMethod("any", "bool hasValue() const",
        [](GenericCall& call) { call.SetReturnBool(RequireAny(call)->HasValue()); });
    ok = ok && engine.RegisterObjectMethod("any", "string typeName() const",
        [](GenericCall& call) {
            call.SetReturnString(RequireAny(call)->StoredValue().Type().Name());
        });
    ok = ok && engine.RegisterObjectMethod("any", "void clear()",
        [](GenericCall& call) { RequireAny(call)->Clear(); });
    return ok;
}

} // namespace mini_as::addons
