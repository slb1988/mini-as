#pragma once

#include "mini_as/core.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mini_as {

class GarbageCollector;

struct TypeInfo {
    std::string name;
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

private:
    std::atomic<std::size_t> refCount_{0};
    const TypeInfo* type_;
};

class ScriptObject final : public RefObject {
public:
    explicit ScriptObject(const TypeInfo* type);
    const Value& GetField(std::size_t index) const;
    void SetField(std::size_t index, Value value);
    std::size_t FieldCount() const;
    bool Implements(std::string_view interfaceName) const;
    std::string ResolveInterfaceMethod(std::string_view interfaceName,
                                       std::string_view declaration) const;
    void EnumerateReferences(const std::function<void(RefObject*)>& visitor) const override;
    void ClearReferences();

private:
    ~ScriptObject() override = default;
    std::vector<Value> fields_;
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
