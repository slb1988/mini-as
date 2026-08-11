#include "mini_as/addons/dictionary.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mini_as::addons {
namespace {

void VisitValue(const Value& value, const std::function<void(RefObject*)>& visitor) {
    if (const auto* object = std::get_if<ObjectHandle>(&value.Raw())) {
        if (*object) visitor(object->Get());
        return;
    }
    if (const auto* function = std::get_if<FunctionHandle>(&value.Raw())) {
        if (function->object) visitor(function->object.Get());
        for (const auto& capture : function->captures)
            if (capture) VisitValue(capture->value, visitor);
        return;
    }
    if (const auto* cell = std::get_if<CapturedCellHandle>(&value.Raw()))
        if (*cell) VisitValue((*cell)->value, visitor);
}

ScriptDictionary* RequireDictionary(GenericCall& call) {
    auto* dictionary = dynamic_cast<ScriptDictionary*>(call.GetObject().Get());
    if (!dictionary) throw std::runtime_error("invalid dictionary receiver");
    return dictionary;
}

std::size_t IteratorIndex(const GenericCall& call, std::size_t argument) {
    const std::uint64_t value = call.GetArg(argument).UnsignedInteger();
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        throw std::runtime_error("dictionary iterator is out of range");
    return static_cast<std::size_t>(value);
}

Value WrapDictionaryValue(Value value) {
    return Value::HostValue("dictionaryValue", DictionaryValue{std::move(value)});
}

const Value& UnwrapDictionaryValue(const Value& value) {
    return value.AsHostValue<DictionaryValue>().value;
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

void RegisterSetOverload(ScriptEngine& engine, std::string_view typeName) {
    const std::string declaration = "void set(string key, " + std::string(typeName) + " value)";
    if (!engine.RegisterObjectMethod("dictionary", declaration,
        [](GenericCall& call) {
            RequireDictionary(call)->Set(call.GetArgString(0), call.GetArg(1));
        })) throw std::runtime_error("dictionary set overload registration failed");
}

void RegisterGetOverload(ScriptEngine& engine, const DataType& type) {
    const std::string declaration = "bool get(string key, " + type.Name() + " &out value) const";
    if (!engine.RegisterObjectMethod("dictionary", declaration,
        [type](GenericCall& call) {
            Value stored;
            Value converted;
            const bool found = RequireDictionary(call)->Get(call.GetArgString(0), stored) &&
                               ConvertStoredValue(stored, type, converted);
            if (found) call.SetArg(1, std::move(converted));
            call.SetReturnBool(found);
        })) throw std::runtime_error("dictionary get overload registration failed");
}

} // namespace

ScriptDictionary::ScriptDictionary(const TypeInfo* type) : RefObject(type) {
    if (type && type->collector) type->collector->Register(this);
}

void ScriptDictionary::Set(std::string key, Value value) {
    values_.insert_or_assign(std::move(key), std::move(value));
}

bool ScriptDictionary::Get(std::string_view key, Value& value) const {
    const auto found = values_.find(key);
    if (found == values_.end()) return false;
    value = found->second;
    return true;
}

bool ScriptDictionary::Exists(std::string_view key) const {
    return values_.find(key) != values_.end();
}

bool ScriptDictionary::Empty() const { return values_.empty(); }
std::size_t ScriptDictionary::Size() const { return values_.size(); }
bool ScriptDictionary::Delete(std::string_view key) {
    const auto found = values_.find(key);
    if (found == values_.end()) return false;
    values_.erase(found);
    return true;
}
void ScriptDictionary::DeleteAll() { values_.clear(); }

std::vector<std::string> ScriptDictionary::Keys() const {
    std::vector<std::string> result;
    result.reserve(values_.size());
    for (const auto& entry : values_) result.push_back(entry.first);
    return result;
}

Value ScriptDictionary::ValueAt(std::size_t iterator) const {
    if (iterator >= values_.size()) throw std::runtime_error("dictionary iterator is out of range");
    auto found = values_.begin();
    std::advance(found, static_cast<std::ptrdiff_t>(iterator));
    return found->second;
}

const std::string& ScriptDictionary::KeyAt(std::size_t iterator) const {
    if (iterator >= values_.size()) throw std::runtime_error("dictionary iterator is out of range");
    auto found = values_.begin();
    std::advance(found, static_cast<std::ptrdiff_t>(iterator));
    return found->first;
}

void ScriptDictionary::EnumerateReferences(
    const std::function<void(RefObject*)>& visitor) const {
    for (const auto& entry : values_) VisitValue(entry.second, visitor);
}

void ScriptDictionary::ClearReferences() { values_.clear(); }

bool RegisterScriptDictionary(ScriptEngine& engine) {
    if (!engine.GetTypeInfo("array<T>")) return false;
    if (!engine.RegisterValueType("dictionaryValue",
            Value::HostValue("dictionaryValue", DictionaryValue{}))) return false;
    const TypeInfo* type = engine.RegisterObjectType("dictionary", true);
    if (!type) return false;

    bool ok = engine.RegisterObjectFactory("dictionary", "dictionary@ f()",
        [type](GenericCall& call) {
            call.SetReturnObject(ObjectHandle(new ScriptDictionary(type)));
        });
    ok = ok && engine.RegisterObjectMethod("dictionaryValue", "int64 opConv() const",
        [](GenericCall& call) {
            Value converted;
            if (!ConvertStoredValue(
                    call.GetObjectValue().AsHostValue<DictionaryValue>().value,
                    DataType::Int64(), converted))
                throw std::runtime_error("dictionary value cannot convert to int64");
            call.SetReturn(std::move(converted));
        });
    ok = ok && engine.RegisterObjectMethod("dictionaryValue", "string typeName() const",
        [](GenericCall& call) {
            call.SetReturnString(
                call.GetObjectValue().AsHostValue<DictionaryValue>().value.Type().Name());
        });

    try {
        RegisterSetOverload(engine, "int64");
        RegisterSetOverload(engine, "double");
        RegisterSetOverload(engine, "string");
        RegisterSetOverload(engine, "bool");
        RegisterGetOverload(engine, DataType::Int64());
        RegisterGetOverload(engine, DataType::Double());
        RegisterGetOverload(engine, DataType::String());
        RegisterGetOverload(engine, DataType::Bool());
    } catch (const std::exception&) {
        return false;
    }

    ok = ok && engine.RegisterObjectMethod("dictionary", "bool exists(string key) const",
        [](GenericCall& call) {
            call.SetReturnBool(RequireDictionary(call)->Exists(call.GetArgString(0)));
        });
    ok = ok && engine.RegisterObjectMethod("dictionary", "bool isEmpty() const",
        [](GenericCall& call) { call.SetReturnBool(RequireDictionary(call)->Empty()); });
    ok = ok && engine.RegisterObjectMethod("dictionary", "uint getSize() const",
        [](GenericCall& call) {
            call.SetReturn(Value::Integer(DataType::UInt(), RequireDictionary(call)->Size()));
        });
    ok = ok && engine.RegisterObjectMethod("dictionary", "bool delete(string key)",
        [](GenericCall& call) {
            call.SetReturnBool(RequireDictionary(call)->Delete(call.GetArgString(0)));
        });
    ok = ok && engine.RegisterObjectMethod("dictionary", "void deleteAll()",
        [](GenericCall& call) { RequireDictionary(call)->DeleteAll(); });
    ok = ok && engine.RegisterObjectMethod("dictionary", "array<string>@ getKeys() const",
        [&engine](GenericCall& call) {
            const TypeInfo* arrayType = engine.GetTypeInfo("array<string>");
            if (!arrayType) throw std::runtime_error("array<string> is not available");
            auto* result = new ScriptArray(arrayType, DataType::String(), Value(std::string{}));
            for (const auto& key : RequireDictionary(call)->Keys()) result->InsertLast(Value(key));
            call.SetReturnObject(ObjectHandle(result));
        });

    ok = ok && engine.RegisterObjectMethod("dictionary", "uint opForBegin() const",
        [](GenericCall& call) { call.SetReturn(Value::Integer(DataType::UInt(), 0)); });
    ok = ok && engine.RegisterObjectMethod("dictionary", "bool opForEnd(uint iterator) const",
        [](GenericCall& call) {
            call.SetReturnBool(IteratorIndex(call, 0) >= RequireDictionary(call)->Size());
        });
    ok = ok && engine.RegisterObjectMethod("dictionary", "uint opForNext(uint iterator) const",
        [](GenericCall& call) {
            call.SetReturn(Value::Integer(
                DataType::UInt(), call.GetArg(0).UnsignedInteger() + 1));
        });
    ok = ok && engine.RegisterObjectMethod(
        "dictionary", "dictionaryValue opForValue0(uint iterator) const",
        [](GenericCall& call) {
            call.SetReturn(WrapDictionaryValue(
                RequireDictionary(call)->ValueAt(IteratorIndex(call, 0))));
        });
    ok = ok && engine.RegisterObjectMethod(
        "dictionary", "string opForValue1(uint iterator) const",
        [](GenericCall& call) {
            call.SetReturnString(RequireDictionary(call)->KeyAt(IteratorIndex(call, 0)));
        });

    ok = ok && engine.RegisterObjectMethod("dictionary", "dictionaryValue get(string key) const",
        [](GenericCall& call) {
            Value value;
            RequireDictionary(call)->Get(call.GetArgString(0), value);
            call.SetReturn(WrapDictionaryValue(std::move(value)));
        });
    ok = ok && engine.RegisterObjectMethod(
        "dictionary", "dictionaryValue set(string key, dictionaryValue value)",
        [](GenericCall& call) {
            const Value value = UnwrapDictionaryValue(call.GetArg(1));
            RequireDictionary(call)->Set(call.GetArgString(0), value);
            call.SetReturn(WrapDictionaryValue(value));
        });
    return ok;
}

} // namespace mini_as::addons
