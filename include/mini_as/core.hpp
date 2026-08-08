#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace mini_as {

class RefObject;

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
    Float, String, Object, Invalid
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
    static DataType String();
    static DataType Object(std::string name, bool handle = false);
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

struct IntegerStorage {
    TypeKind kind = TypeKind::Int;
    std::uint64_t bits = 0;
};

bool operator==(const IntegerStorage& left, const IntegerStorage& right);

class Value {
public:
    using Storage = std::variant<std::monostate, bool, std::int32_t, IntegerStorage,
                                 float, std::string, ObjectHandle>;

    Value() = default;
    explicit Value(bool value);
    explicit Value(std::int32_t value);
    explicit Value(float value);
    explicit Value(std::string value);
    explicit Value(const char* value);
    explicit Value(ObjectHandle value);

    static Value Integer(const DataType& type, std::uint64_t bits);

    DataType Type() const;
    bool IsVoid() const;
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

Value ConvertInteger(const Value& value, const DataType& target);

bool operator==(const Value& left, const Value& right);

} // namespace mini_as
