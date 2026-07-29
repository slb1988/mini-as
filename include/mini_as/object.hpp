#pragma once

#include "mini_as/core.hpp"

#include <atomic>
#include <functional>
#include <string>

namespace mini_as {

struct TypeInfo {
    std::string name;
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
    virtual ~RefObject() = default;

private:
    std::atomic<std::size_t> refCount_{0};
    const TypeInfo* type_;
};

template <typename T, typename... Args>
ObjectHandle MakeObject(const TypeInfo* type, Args&&... args) {
    return ObjectHandle(new T(type, std::forward<Args>(args)...));
}

} // namespace mini_as

