#include "mini_as/object.hpp"

#include <utility>
#include <algorithm>
#include <limits>
#include <unordered_set>

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

WeakObjectHandle::WeakObjectHandle(std::string typeName, bool readOnly)
    : typeName_(std::move(typeName)), readOnly_(readOnly) {}

WeakObjectHandle::WeakObjectHandle(const ObjectHandle& object, std::string typeName, bool readOnly)
    : object_(object.Get()), state_(object_ ? object_->GetWeakRefState() : nullptr),
      typeName_(std::move(typeName)), readOnly_(readOnly) {}

ObjectHandle WeakObjectHandle::Lock() const {
    if (!object_ || !state_) return {};
    std::lock_guard<std::mutex> guard(state_->mutex);
    return state_->alive ? ObjectHandle(object_) : ObjectHandle{};
}

bool WeakObjectHandle::Expired() const {
    if (!object_ || !state_) return true;
    std::lock_guard<std::mutex> guard(state_->mutex);
    return !state_->alive;
}

WeakObjectHandle WeakObjectHandle::AsReadOnly() const {
    WeakObjectHandle result = *this;
    result.readOnly_ = true;
    return result;
}

bool WeakObjectHandle::Equals(const ObjectHandle& object) const {
    if (!object_) return !object;
    return object_ == object.Get() && state_ == object_->GetWeakRefState();
}

bool WeakObjectHandle::SameTarget(const WeakObjectHandle& other) const {
    return object_ == other.object_ && state_ == other.state_ && typeName_ == other.typeName_;
}

const std::string& WeakObjectHandle::TypeName() const { return typeName_; }
bool WeakObjectHandle::IsReadOnly() const { return readOnly_; }

bool operator==(const WeakObjectHandle& left, const WeakObjectHandle& right) {
    return left.object_ == right.object_ && left.state_ == right.state_ &&
           left.typeName_ == right.typeName_ && left.readOnly_ == right.readOnly_;
}

RefObject::RefObject(const TypeInfo* type) : type_(type) {}
RefObject::~RefObject() {
    {
        std::lock_guard<std::mutex> guard(weakRefState_->mutex);
        weakRefState_->alive = false;
    }
    if (type_ && type_->collector) type_->collector->Unregister(this);
}
void RefObject::AddRef() {
    refCount_.fetch_add(1, std::memory_order_relaxed);
    if (type_ && type_->collector) type_->collector->NotifyReferenceChange();
}
void RefObject::Release() {
    if (type_ && type_->collector) type_->collector->NotifyReferenceChange();
    bool destroy = false;
    {
        std::lock_guard<std::mutex> guard(weakRefState_->mutex);
        destroy = refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1;
        if (destroy)
            weakRefState_->alive = false;
    }
    if (destroy) OnZeroReferences();
}
std::size_t RefObject::RefCount() const { return refCount_.load(std::memory_order_relaxed); }
const TypeInfo* RefObject::GetTypeInfo() const { return type_; }
std::shared_ptr<WeakRefState> RefObject::GetWeakRefState() const { return weakRefState_; }
void RefObject::EnumerateReferences(const std::function<void(RefObject*)>&) const {}
void RefObject::ClearReferences() {}
void RefObject::OnZeroReferences() { delete this; }

bool TypeInfo::IsA(std::string_view typeName) const {
    for (const TypeInfo* type = this; type; type = type->baseType)
        if (type->name == typeName) return true;
    return false;
}

ScriptObject::ScriptObject(const TypeInfo* type, ObjectFinalizerQueue* finalizerQueue,
                           ScriptFinalizerBinding finalizer)
    : RefObject(type), finalizerQueue_(finalizerQueue), finalizer_(std::move(finalizer)) {
    if (type && type->collector) type->collector->Register(this);
    if (!type) return;
    fields_.reserve(type->fields.size());
    for (const auto& field : type->fields) {
        switch (field.second.kind) {
        case TypeKind::Bool: fields_.emplace_back(false); break;
        case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int: case TypeKind::Int64:
        case TypeKind::Enum:
        case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt: case TypeKind::UInt64:
            fields_.push_back(Value::Integer(field.second, 0)); break;
        case TypeKind::Float: fields_.emplace_back(0.0f); break;
        case TypeKind::Double: fields_.emplace_back(0.0); break;
        case TypeKind::String: fields_.emplace_back(std::string{}); break;
        case TypeKind::Object: fields_.emplace_back(ObjectHandle{}); break;
        case TypeKind::Function:
            fields_.emplace_back(FunctionHandle{{}, {}, field.second.objectName, false}); break;
        case TypeKind::WeakRef: case TypeKind::ConstWeakRef:
            fields_.emplace_back(WeakObjectHandle(field.second.objectName,
                field.second.kind == TypeKind::ConstWeakRef)); break;
        default: fields_.emplace_back(); break;
        }
    }
}

const Value& ScriptObject::GetField(std::size_t index) const { return fields_.at(index); }
void ScriptObject::SetField(std::size_t index, Value value) { fields_.at(index) = std::move(value); }
bool ScriptObject::CopyFieldsFrom(const ScriptObject& source) {
    const TypeInfo* destinationType = GetTypeInfo();
    if (!destinationType || !source.IsA(destinationType->name) ||
        source.fields_.size() < fields_.size()) return false;
    std::vector<Value> copied(source.fields_.begin(),
                              source.fields_.begin() + static_cast<std::ptrdiff_t>(fields_.size()));
    fields_ = std::move(copied);
    return true;
}
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
    for (const auto& value : fields_) value.EnumerateReferences(visitor);
}

void ScriptObject::ClearReferences() {
    for (auto& value : fields_) value.ClearReferences();
}

bool ScriptObject::IsA(std::string_view typeName) const {
    const auto* type = GetTypeInfo();
    return type && type->IsA(typeName);
}

const ScriptFinalizerBinding& ScriptObject::Finalizer() const { return finalizer_; }

void ScriptObject::OnZeroReferences() {
    if (finalizerQueue_ && !finalizer_.functions.empty() && finalizer_.module &&
        !finalizerQueued_) {
        finalizerQueued_ = true;
        AddRef();
        finalizerQueue_->EnqueueFinalizer(this);
        return;
    }
    delete this;
}

void GarbageCollector::Register(RefObject* object) {
    if (object && candidates_.insert(object).second && !suppressNotifications_)
        ++mutationGeneration_;
}
void GarbageCollector::Unregister(RefObject* object) {
    if (candidates_.erase(object) != 0 && !suppressNotifications_)
        ++mutationGeneration_;
}
void GarbageCollector::NotifyReferenceChange() {
    if (!suppressNotifications_) ++mutationGeneration_;
}
std::size_t GarbageCollector::TrackedCount() const { return candidates_.size(); }

void GarbageCollector::BeginCycle() {
    snapshot_.assign(candidates_.begin(), candidates_.end());
    internalIncoming_.clear();
    reachable_.clear();
    work_.clear();
    garbage_.clear();
    for (auto* object : snapshot_) internalIncoming_[object] = 0;
    cursor_ = 0;
    cycleGeneration_ = mutationGeneration_;
    phase_ = snapshot_.empty() ? Phase::Idle : Phase::CountIncoming;
}

void GarbageCollector::ResetCycle() {
    phase_ = Phase::Idle;
    snapshot_.clear();
    internalIncoming_.clear();
    reachable_.clear();
    work_.clear();
    garbage_.clear();
    cursor_ = 0;
}

std::size_t GarbageCollector::DestroyGarbage() {
    suppressNotifications_ = true;
    for (auto* object : garbage_) object->AddRef();
    for (auto* object : garbage_) object->ClearReferences();
    for (auto* object : garbage_) object->Release();
    suppressNotifications_ = false;
    const std::size_t destroyed = garbage_.size();
    ++mutationGeneration_;
    ResetCycle();
    return destroyed;
}

std::size_t GarbageCollector::CollectStep(std::size_t workBudget) {
    if (workBudget == 0) return 0;
    if (phase_ != Phase::Idle && cycleGeneration_ != mutationGeneration_) ResetCycle();
    if (phase_ == Phase::Idle) BeginCycle();
    while (phase_ != Phase::Idle && workBudget > 0) {
        if (phase_ == Phase::CountIncoming) {
            RefObject* object = snapshot_[cursor_++];
            object->EnumerateReferences([&](RefObject* target) {
                const auto found = internalIncoming_.find(target);
                if (found != internalIncoming_.end()) ++found->second;
            });
            --workBudget;
            if (cursor_ == snapshot_.size()) { cursor_ = 0; phase_ = Phase::SeedRoots; }
            continue;
        }
        if (phase_ == Phase::SeedRoots) {
            RefObject* object = snapshot_[cursor_++];
            if (object->RefCount() > internalIncoming_[object] && reachable_.insert(object).second)
                work_.push_back(object);
            --workBudget;
            if (cursor_ == snapshot_.size()) { cursor_ = 0; phase_ = Phase::MarkReachable; }
            continue;
        }
        if (phase_ == Phase::MarkReachable) {
            if (work_.empty()) { cursor_ = 0; phase_ = Phase::SelectGarbage; continue; }
            RefObject* object = work_.back();
            work_.pop_back();
            object->EnumerateReferences([&](RefObject* target) {
                if (internalIncoming_.find(target) != internalIncoming_.end() &&
                    reachable_.insert(target).second) work_.push_back(target);
            });
            --workBudget;
            continue;
        }
        RefObject* object = snapshot_[cursor_++];
        if (reachable_.find(object) == reachable_.end()) garbage_.push_back(object);
        --workBudget;
        if (cursor_ == snapshot_.size()) return DestroyGarbage();
    }
    return 0;
}

bool GarbageCollector::CycleInProgress() const { return phase_ != Phase::Idle; }

std::size_t GarbageCollector::Collect() {
    std::size_t collected = 0;
    do {
        collected += CollectStep(std::numeric_limits<std::size_t>::max());
    } while (CycleInProgress());
    return collected;
}

} // namespace mini_as
