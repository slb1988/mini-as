#pragma once

#include "mini_as/core.hpp"
#include "mini_as/symbols.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mini_as {

class GarbageCollector;
struct BytecodeModule;
struct ModuleState;
class ScriptObject;
struct RegisteredHostObjectProperty;

struct ScriptFinalizerBinding {
    std::vector<FunctionId> functions;
    std::shared_ptr<const BytecodeModule> module;
    std::weak_ptr<ModuleState> state;
};

class ObjectFinalizerQueue {
public:
    virtual ~ObjectFinalizerQueue() = default;
    virtual void EnqueueFinalizer(ScriptObject* object) = 0;
};

struct TypeInfo {
    std::string name;
    TypeId id;
    bool script = false;
    bool host = false;
    bool valueType = false;
    bool templateDefinition = false;
    std::string templateBase;
    std::vector<std::string> templateParameters;
    std::vector<DataType> templateSubTypes;
    std::uint32_t accessMask = ~std::uint32_t{0};
    std::string configGroup;
    bool active = true;
    Value defaultValue;
    std::string baseClass;
    const TypeInfo* baseType = nullptr;
    std::vector<std::pair<std::string, DataType>> fields;
    std::vector<const RegisteredHostObjectProperty*> hostProperties;
    std::vector<std::string> interfaces;
    std::unordered_map<std::string, std::string> interfaceMethodTable;
    GarbageCollector* collector = nullptr;

    bool IsA(std::string_view typeName) const;
};

class RefObject {
public:
    explicit RefObject(const TypeInfo* type);
    void AddRef();
    void Release();
    std::size_t RefCount() const;
    const TypeInfo* GetTypeInfo() const;
    std::shared_ptr<WeakRefState> GetWeakRefState() const;
    virtual void EnumerateReferences(const std::function<void(RefObject*)>& visitor) const;
    virtual void ClearReferences();

protected:
    virtual ~RefObject();
    virtual void OnZeroReferences();

private:
    std::atomic<std::size_t> refCount_{0};
    const TypeInfo* type_;
    std::shared_ptr<WeakRefState> weakRefState_ = std::make_shared<WeakRefState>();
};

class ScriptObject final : public RefObject {
public:
    explicit ScriptObject(const TypeInfo* type, ObjectFinalizerQueue* finalizerQueue = nullptr,
                          ScriptFinalizerBinding finalizer = {});
    const Value& GetField(std::size_t index) const;
    void SetField(std::size_t index, Value value);
    bool CopyFieldsFrom(const ScriptObject& source);
    std::size_t FieldCount() const;
    bool Implements(std::string_view interfaceName) const;
    bool IsA(std::string_view typeName) const;
    std::string ResolveInterfaceMethod(std::string_view interfaceName,
                                       std::string_view declaration) const;
    void EnumerateReferences(const std::function<void(RefObject*)>& visitor) const override;
    void ClearReferences() override;
    const ScriptFinalizerBinding& Finalizer() const;

private:
    ~ScriptObject() override = default;
    void OnZeroReferences() override;
    std::vector<Value> fields_;
    ObjectFinalizerQueue* finalizerQueue_ = nullptr;
    ScriptFinalizerBinding finalizer_;
    bool finalizerQueued_ = false;
};

class GarbageCollector {
public:
    void Register(RefObject* object);
    void Unregister(RefObject* object);
    void NotifyReferenceChange();
    std::size_t Collect();
    std::size_t CollectStep(std::size_t workBudget = 1);
    bool CycleInProgress() const;
    std::size_t TrackedCount() const;

private:
    enum class Phase { Idle, CountIncoming, SeedRoots, MarkReachable, SelectGarbage };
    void BeginCycle();
    void ResetCycle();
    std::size_t DestroyGarbage();
    std::unordered_set<RefObject*> candidates_;
    Phase phase_ = Phase::Idle;
    std::vector<RefObject*> snapshot_;
    std::unordered_map<RefObject*, std::size_t> internalIncoming_;
    std::unordered_set<RefObject*> reachable_;
    std::vector<RefObject*> work_;
    std::vector<RefObject*> garbage_;
    std::size_t cursor_ = 0;
    std::uint64_t mutationGeneration_ = 0;
    std::uint64_t cycleGeneration_ = 0;
    bool suppressNotifications_ = false;
};

template <typename T, typename... Args>
ObjectHandle MakeObject(const TypeInfo* type, Args&&... args) {
    return ObjectHandle(new T(type, std::forward<Args>(args)...));
}

} // namespace mini_as
