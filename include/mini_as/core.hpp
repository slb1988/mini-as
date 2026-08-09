#pragma once

#include "mini_as/symbols.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mini_as {

class RefObject;
struct WeakRefState {
    std::mutex mutex;
    bool alive = true;
};
struct CapturedCell;
using CapturedCellHandle = std::shared_ptr<CapturedCell>;

class ObjectHandle {
public:
    ObjectHandle() = default;
    explicit ObjectHandle(RefObject* object);
    ObjectHandle(const ObjectHandle& other);
    ObjectHandle(ObjectHandle&& other) noexcept;
    ~ObjectHandle();
    ObjectHandle& operator=(const ObjectHandle& other);
    ObjectHandle& operator=(ObjectHandle&& other) noexcept;
    RefObject* Get() const;
    explicit operator bool() const;

private:
    RefObject* object_ = nullptr;
};

bool operator==(const ObjectHandle& left, const ObjectHandle& right);

struct SourceLocation {
    std::string section;
    std::size_t offset = 0;
    int row = 1;
    int column = 1;
};

enum class Severity { Info, Warning, Error };

struct Diagnostic {
    SourceLocation location;
    Severity severity = Severity::Error;
    std::string message;
};

class DiagnosticSink {
public:
    using Callback = std::function<void(const Diagnostic&)>;

    explicit DiagnosticSink(Callback callback = {});
    void Report(SourceLocation location, Severity severity, std::string message);
    bool HasErrors() const;
    const std::vector<Diagnostic>& All() const;
    void Clear();

private:
    Callback callback_;
    std::vector<Diagnostic> diagnostics_;
};

enum class TypeKind {
    Void, Bool,
    Int8, Int16, Int, Int64,
    UInt8, UInt16, UInt, UInt64,
    Float, Double, String, Enum, Object, Function, WeakRef, ConstWeakRef, Invalid
};

struct DataType {
    TypeKind kind = TypeKind::Invalid;
    std::string objectName;
    bool isHandle = false;

    static DataType Void();
    static DataType Bool();
    static DataType Int8();
    static DataType Int16();
    static DataType Int();
    static DataType Int32();
    static DataType Int64();
    static DataType UInt8();
    static DataType UInt16();
    static DataType UInt();
    static DataType UInt32();
    static DataType UInt64();
    static DataType Float();
    static DataType Double();
    static DataType String();
    static DataType Enum(std::string name);
    static DataType Object(std::string name, bool handle = false);
    static DataType Function(std::string name, bool handle = true);
    static DataType WeakRef(std::string subtype, bool readOnly = false);
    static DataType Invalid();

    std::string Name() const;
    bool IsNumeric() const;
    bool IsInteger() const;
    bool IsSignedInteger() const;
    bool IsUnsignedInteger() const;
    unsigned IntegerBits() const;
    bool IsValid() const;
};

bool operator==(const DataType& left, const DataType& right);
bool operator!=(const DataType& left, const DataType& right);

DataType CommonNumericType(const DataType& left, const DataType& right);

enum class ReferenceKind { Global, Field };

struct ReferenceStorage {
    ReferenceKind kind = ReferenceKind::Global;
    DataType type = DataType::Invalid();
    std::uint32_t slot = 0;
    ObjectHandle object;
};

bool operator==(const ReferenceStorage& left, const ReferenceStorage& right);

struct IntegerStorage {
    TypeKind kind = TypeKind::Int;
    std::uint64_t bits = 0;
    std::string typeName;
};

bool operator==(const IntegerStorage& left, const IntegerStorage& right);

class WeakObjectHandle {
public:
    WeakObjectHandle() = default;
    explicit WeakObjectHandle(std::string typeName, bool readOnly = false);
    WeakObjectHandle(const ObjectHandle& object, std::string typeName, bool readOnly = false);

    ObjectHandle Lock() const;
    bool Expired() const;
    WeakObjectHandle AsReadOnly() const;
    bool Equals(const ObjectHandle& object) const;
    bool SameTarget(const WeakObjectHandle& other) const;
    const std::string& TypeName() const;
    bool IsReadOnly() const;

private:
    RefObject* object_ = nullptr;
    std::shared_ptr<WeakRefState> state_;
    std::string typeName_;
    bool readOnly_ = false;

    friend bool operator==(const WeakObjectHandle&, const WeakObjectHandle&);
};

bool operator==(const WeakObjectHandle& left, const WeakObjectHandle& right);

struct FunctionHandle {
    FunctionHandle(FunctionId function = {}, TypeId signature = {}, std::string typeName = {},
                   bool host = false, ObjectHandle object = {}, TypeId dispatchType = {},
                   std::uint32_t virtualSlot = 0, bool virtualMethod = false,
                   std::vector<CapturedCellHandle> captures = {})
        : function(function), signature(signature), typeName(std::move(typeName)), host(host),
          object(std::move(object)), dispatchType(dispatchType), virtualSlot(virtualSlot),
          virtualMethod(virtualMethod), captures(std::move(captures)) {}

    FunctionId function;
    TypeId signature;
    std::string typeName;
    bool host = false;
    ObjectHandle object;
    TypeId dispatchType;
    std::uint32_t virtualSlot = 0;
    bool virtualMethod = false;
    std::vector<CapturedCellHandle> captures;

    explicit operator bool() const { return function.IsValid() || (virtualMethod && object); }
};

bool operator==(const FunctionHandle& left, const FunctionHandle& right);

class Value {
public:
    using Storage = std::variant<std::monostate, bool, std::int32_t, IntegerStorage,
                                 float, double, std::string, ObjectHandle, FunctionHandle,
                                 WeakObjectHandle, CapturedCellHandle, ReferenceStorage>;

    Value() = default;
    explicit Value(bool value);
    explicit Value(std::int32_t value);
    explicit Value(float value);
    explicit Value(double value);
    explicit Value(std::string value);
    explicit Value(const char* value);
    explicit Value(ObjectHandle value);
    explicit Value(FunctionHandle value);
    explicit Value(WeakObjectHandle value);
    explicit Value(CapturedCellHandle value);
    explicit Value(ReferenceStorage value);

    static Value Integer(const DataType& type, std::uint64_t bits);

    DataType Type() const;
    bool IsVoid() const;
    bool IsReference() const;
    std::int64_t SignedInteger() const;
    std::uint64_t UnsignedInteger() const;
    const Storage& Raw() const;

    template <typename T>
    const T& As() const {
        if (const auto* value = std::get_if<T>(&storage_)) {
            return *value;
        }
        throw std::runtime_error("value has unexpected type");
    }

    std::string ToString() const;

private:
    Storage storage_;
};

struct CapturedCell {
    Value value;
};

Value ConvertInteger(const Value& value, const DataType& target);

bool operator==(const Value& left, const Value& right);

} // namespace mini_as
