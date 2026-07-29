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

enum class TypeKind { Void, Bool, Int, Float, String, Object, Invalid };

struct DataType {
    TypeKind kind = TypeKind::Invalid;
    std::string objectName;
    bool isHandle = false;

    static DataType Void();
    static DataType Bool();
    static DataType Int();
    static DataType Float();
    static DataType String();
    static DataType Object(std::string name, bool handle = false);
    static DataType Invalid();

    std::string Name() const;
    bool IsNumeric() const;
    bool IsValid() const;
};

bool operator==(const DataType& left, const DataType& right);
bool operator!=(const DataType& left, const DataType& right);

class Value {
public:
    using Storage = std::variant<std::monostate, bool, std::int32_t, float, std::string, ObjectHandle>;

    Value() = default;
    explicit Value(bool value);
    explicit Value(std::int32_t value);
    explicit Value(float value);
    explicit Value(std::string value);
    explicit Value(const char* value);
    explicit Value(ObjectHandle value);

    DataType Type() const;
    bool IsVoid() const;
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

bool operator==(const Value& left, const Value& right);

} // namespace mini_as
