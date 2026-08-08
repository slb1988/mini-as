#include "mini_as/object.hpp"

#include <utility>
#include <algorithm>

namespace mini_as {

ObjectHandle::ObjectHandle(RefObject* object) : object_(object) { if (object_) object_->AddRef(); }
ObjectHandle::ObjectHandle(const ObjectHandle& other) : ObjectHandle(other.object_) {}
ObjectHandle::ObjectHandle(ObjectHandle&& other) noexcept : object_(other.object_) { other.object_ = nullptr; }
ObjectHandle::~ObjectHandle() { if (object_) object_->Release(); }

ObjectHandle& ObjectHandle::operator=(const ObjectHandle& other) {
    if (this == &other) return *this;
    RefObject* replacement = other.object_;
    if (replacement) replacement->AddRef();
    if (object_) object_->Release();
    object_ = replacement;
    return *this;
}

ObjectHandle& ObjectHandle::operator=(ObjectHandle&& other) noexcept {
    if (this == &other) return *this;
    if (object_) object_->Release();
    object_ = other.object_;
    other.object_ = nullptr;
    return *this;
}

RefObject* ObjectHandle::Get() const { return object_; }
ObjectHandle::operator bool() const { return object_ != nullptr; }
bool operator==(const ObjectHandle& left, const ObjectHandle& right) { return left.Get() == right.Get(); }

RefObject::RefObject(const TypeInfo* type) : type_(type) {}
RefObject::~RefObject() { if (type_ && type_->collector) type_->collector->Unregister(this); }
void RefObject::AddRef() { refCount_.fetch_add(1, std::memory_order_relaxed); }
void RefObject::Release() {
    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
}
std::size_t RefObject::RefCount() const { return refCount_.load(std::memory_order_relaxed); }
const TypeInfo* RefObject::GetTypeInfo() const { return type_; }
void RefObject::EnumerateReferences(const std::function<void(RefObject*)>&) const {}

ScriptObject::ScriptObject(const TypeInfo* type) : RefObject(type) {
    if (type && type->collector) type->collector->Register(this);
    if (!type) return;
    fields_.reserve(type->fields.size());
    for (const auto& field : type->fields) {
        switch (field.second.kind) {
        case TypeKind::Bool: fields_.emplace_back(false); break;
        case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int: case TypeKind::Int64:
        case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt: case TypeKind::UInt64:
            fields_.push_back(Value::Integer(field.second, 0)); break;
        case TypeKind::Float: fields_.emplace_back(0.0f); break;
        case TypeKind::String: fields_.emplace_back(std::string{}); break;
        case TypeKind::Object: fields_.emplace_back(ObjectHandle{}); break;
        default: fields_.emplace_back(); break;
        }
    }
}

const Value& ScriptObject::GetField(std::size_t index) const { return fields_.at(index); }
void ScriptObject::SetField(std::size_t index, Value value) { fields_.at(index) = std::move(value); }
std::size_t ScriptObject::FieldCount() const { return fields_.size(); }

bool ScriptObject::Implements(std::string_view interfaceName) const {
    const auto* type = GetTypeInfo();
    return type && std::find(type->interfaces.begin(), type->interfaces.end(), interfaceName) != type->interfaces.end();
}

std::string ScriptObject::ResolveInterfaceMethod(std::string_view interfaceName,
                                                 std::string_view declaration) const {
    if (!Implements(interfaceName)) return {};
    const auto& table = GetTypeInfo()->interfaceMethodTable;
    const auto found = table.find(std::string(interfaceName) + "::" + std::string(declaration));
    return found == table.end() ? std::string{} : found->second;
}

void ScriptObject::EnumerateReferences(const std::function<void(RefObject*)>& visitor) const {
    for (const auto& value : fields_) {
        if (value.Type().kind != TypeKind::Object) continue;
        const auto& handle = value.As<ObjectHandle>();
        if (handle) visitor(handle.Get());
    }
}

void ScriptObject::ClearReferences() {
    for (auto& value : fields_) {
        if (value.Type().kind == TypeKind::Object) value = Value(ObjectHandle{});
    }
}

void GarbageCollector::Register(RefObject* object) { if (object) candidates_.insert(object); }
void GarbageCollector::Unregister(RefObject* object) { candidates_.erase(object); }
std::size_t GarbageCollector::TrackedCount() const { return candidates_.size(); }

std::size_t GarbageCollector::Collect() {
    std::vector<RefObject*> candidates(candidates_.begin(), candidates_.end());
    std::unordered_map<RefObject*, std::size_t> internalIncoming;
    for (auto* object : candidates) internalIncoming[object] = 0;
    for (auto* object : candidates) {
        object->EnumerateReferences([&](RefObject* target) {
            const auto found = internalIncoming.find(target);
            if (found != internalIncoming.end()) ++found->second;
        });
    }

    std::unordered_set<RefObject*> reachable;
    std::vector<RefObject*> work;
    for (auto* object : candidates) {
        if (object->RefCount() > internalIncoming[object]) {
            reachable.insert(object);
            work.push_back(object);
        }
    }
    while (!work.empty()) {
        RefObject* object = work.back();
        work.pop_back();
        object->EnumerateReferences([&](RefObject* target) {
            if (internalIncoming.find(target) != internalIncoming.end() && reachable.insert(target).second)
                work.push_back(target);
        });
    }

    std::vector<RefObject*> garbage;
    for (auto* object : candidates) if (reachable.find(object) == reachable.end()) garbage.push_back(object);
    for (auto* object : garbage) object->AddRef();
    for (auto* object : garbage) {
        if (auto* scriptObject = dynamic_cast<ScriptObject*>(object)) scriptObject->ClearReferences();
    }
    for (auto* object : garbage) object->Release();
    return garbage.size();
}

} // namespace mini_as
