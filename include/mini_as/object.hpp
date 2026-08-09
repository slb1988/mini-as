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

struct ScriptFinalizerBinding {
    FunctionId function;
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
    std::vector<std::pair<std::string, DataType>> fields;
    std::vector<std::string> interfaces;
    std::unordered_map<std::string, std::string> interfaceMethodTable;
    GarbageCollector* collector = nullptr;
};

class RefObject {
public:
    explicit RefObject(const TypeInfo* type);
    void AddRef();
    void Release();
    std::size_t RefCount() const;
    const TypeInfo* GetTypeInfo() const;
    virtual void EnumerateReferences(const std::function<void(RefObject*)>& visitor) const;

protected:
    virtual ~RefObject();
    virtual void OnZeroReferences();

private:
    std::atomic<std::size_t> refCount_{0};
    const TypeInfo* type_;
};

class ScriptObject final : public RefObject {
public:
    explicit ScriptObject(const TypeInfo* type, ObjectFinalizerQueue* finalizerQueue = nullptr,
                          ScriptFinalizerBinding finalizer = {});
    const Value& GetField(std::size_t index) const;
    void SetField(std::size_t index, Value value);
    std::size_t FieldCount() const;
    bool Implements(std::string_view interfaceName) const;
    std::string ResolveInterfaceMethod(std::string_view interfaceName,
                                       std::string_view declaration) const;
    void EnumerateReferences(const std::function<void(RefObject*)>& visitor) const override;
    void ClearReferences();
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
    std::size_t Collect();
    std::size_t TrackedCount() const;

private:
    std::unordered_set<RefObject*> candidates_;
};

template <typename T, typename... Args>
ObjectHandle MakeObject(const TypeInfo* type, Args&&... args) {
    return ObjectHandle(new T(type, std::forward<Args>(args)...));
}

} // namespace mini_as
