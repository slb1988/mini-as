#include "mini_as/addons/array.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace mini_as::addons {
namespace {

Value DefaultElement(const DataType& type, const ScriptEngine& engine) {
    if (type == DataType::Bool()) return Value(false);
    if (type.IsInteger()) return Value::Integer(type, 0);
    if (type == DataType::Float()) return Value(0.0f);
    if (type == DataType::Double()) return Value(0.0);
    if (type == DataType::String()) return Value(std::string{});
    if (type.kind == TypeKind::Object) {
        const TypeInfo* object = engine.GetTypeInfo(type.objectName);
        if (!type.isHandle && object && object->valueType) return object->defaultValue;
        return Value(ObjectHandle{});
    }
    if (type.kind == TypeKind::Function)
        return Value(FunctionHandle{{}, {}, type.objectName, false});
    if (type.kind == TypeKind::WeakRef || type.kind == TypeKind::ConstWeakRef)
        return Value(WeakObjectHandle(type.objectName, type.kind == TypeKind::ConstWeakRef));
    return Value{};
}

ScriptArray* RequireArray(GenericCall& call) {
    auto* array = dynamic_cast<ScriptArray*>(call.GetObject().Get());
    if (!array) throw std::runtime_error("invalid array receiver");
    return array;
}

std::size_t ArrayIndex(const GenericCall& call, std::size_t argument) {
    const std::uint64_t value = call.GetArg(argument).UnsignedInteger();
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        throw std::runtime_error("array index is out of range");
    return static_cast<std::size_t>(value);
}

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

} // namespace

ScriptArray::ScriptArray(const TypeInfo* type, DataType elementType,
                         Value defaultElement, std::size_t length)
    : RefObject(type), elementType_(std::move(elementType)),
      defaultElement_(std::move(defaultElement)), elements_(length, defaultElement_) {
    if (type && type->collector) type->collector->Register(this);
}

const DataType& ScriptArray::ElementType() const { return elementType_; }
std::size_t ScriptArray::Size() const { return elements_.size(); }
bool ScriptArray::Empty() const { return elements_.empty(); }
void ScriptArray::Resize(std::size_t length) { elements_.resize(length, defaultElement_); }
void ScriptArray::InsertLast(Value value) { elements_.push_back(std::move(value)); }
Value ScriptArray::RemoveLast() {
    if (elements_.empty()) throw std::runtime_error("cannot remove from an empty array");
    Value value = std::move(elements_.back());
    elements_.pop_back();
    return value;
}
const Value& ScriptArray::Get(std::size_t index) const {
    if (index >= elements_.size()) throw std::runtime_error("array index is out of range");
    return elements_[index];
}
void ScriptArray::Set(std::size_t index, Value value) {
    if (index >= elements_.size()) throw std::runtime_error("array index is out of range");
    elements_[index] = std::move(value);
}
void ScriptArray::EnumerateReferences(
    const std::function<void(RefObject*)>& visitor) const {
    for (const auto& element : elements_) VisitValue(element, visitor);
}
void ScriptArray::ClearReferences() {
    for (auto& element : elements_) element = defaultElement_;
    elements_.clear();
    defaultElement_ = Value{};
}

bool RegisterScriptArray(ScriptEngine& engine, std::string name) {
    if (name.empty()) return false;
    const std::string declaration = name + "<class T>";
    return engine.RegisterTemplateType(declaration,
        [](const std::vector<DataType>& subTypes, std::string& reason) {
            if (subTypes.size() != 1 || !subTypes[0].IsValid() ||
                subTypes[0] == DataType::Void()) {
                reason = "array requires one non-void subtype";
                return false;
            }
            if (subTypes[0].kind == TypeKind::Object && !subTypes[0].isHandle) {
                reason = "reference object array subtypes must use handles";
                return false;
            }
            return true;
        },
        [](ScriptEngine& target, const TypeInfo& type, std::string& reason) {
            if (type.templateSubTypes.size() != 1) {
                reason = "array instance is missing its element type";
                return false;
            }
            const DataType elementType = type.templateSubTypes[0];
            const Value defaultElement = DefaultElement(elementType, target);
            const std::string typeName = type.name;
            const auto make = [&type, elementType, defaultElement](std::size_t length) {
                return ObjectHandle(new ScriptArray(
                    &type, elementType, defaultElement, length));
            };
            bool ok = target.RegisterObjectFactory(typeName, typeName + "@ f()",
                [make](GenericCall& call) { call.SetReturnObject(make(0)); });
            ok = ok && target.RegisterObjectFactory(typeName,
                typeName + "@ f(uint length)",
                [make](GenericCall& call) {
                    call.SetReturnObject(make(ArrayIndex(call, 0)));
                });
            ok = ok && target.RegisterObjectMethod(typeName, "uint length() const",
                [](GenericCall& call) {
                    call.SetReturn(Value::Integer(
                        DataType::UInt(), RequireArray(call)->Size()));
                });
            ok = ok && target.RegisterObjectMethod(typeName, "bool isEmpty() const",
                [](GenericCall& call) { call.SetReturnBool(RequireArray(call)->Empty()); });
            ok = ok && target.RegisterObjectMethod(typeName, "void resize(uint length)",
                [](GenericCall& call) { RequireArray(call)->Resize(ArrayIndex(call, 0)); });
            ok = ok && target.RegisterObjectMethod(typeName,
                "void insertLast(" + elementType.Name() + " value)",
                [](GenericCall& call) { RequireArray(call)->InsertLast(call.GetArg(0)); });
            ok = ok && target.RegisterObjectMethod(typeName,
                elementType.Name() + " get(uint index) const",
                [](GenericCall& call) {
                    call.SetReturn(RequireArray(call)->Get(ArrayIndex(call, 0)));
                });
            ok = ok && target.RegisterObjectMethod(typeName,
                "void set(uint index, " + elementType.Name() + " value)",
                [](GenericCall& call) {
                    RequireArray(call)->Set(ArrayIndex(call, 0), call.GetArg(1));
                });
            ok = ok && target.RegisterObjectMethod(typeName,
                elementType.Name() + " removeLast()",
                [](GenericCall& call) {
                    call.SetReturn(RequireArray(call)->RemoveLast());
                });
            if (!ok) reason = "array factories or methods could not be registered";
            return ok;
        }, true) != nullptr;
}

} // namespace mini_as::addons
