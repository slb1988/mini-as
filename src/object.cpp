#include "mini_as/object.hpp"

#include <utility>

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
void RefObject::AddRef() { refCount_.fetch_add(1, std::memory_order_relaxed); }
void RefObject::Release() {
    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
}
std::size_t RefObject::RefCount() const { return refCount_.load(std::memory_order_relaxed); }
const TypeInfo* RefObject::GetTypeInfo() const { return type_; }
void RefObject::EnumerateReferences(const std::function<void(RefObject*)>&) const {}

} // namespace mini_as

