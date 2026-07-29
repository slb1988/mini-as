#include "mini_as/core.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace mini_as {

DiagnosticSink::DiagnosticSink(Callback callback) : callback_(std::move(callback)) {}

void DiagnosticSink::Report(SourceLocation location, Severity severity, std::string message) {
    diagnostics_.push_back({std::move(location), severity, std::move(message)});
    if (callback_) {
        callback_(diagnostics_.back());
    }
}

bool DiagnosticSink::HasErrors() const {
    for (const auto& diagnostic : diagnostics_) {
        if (diagnostic.severity == Severity::Error) {
            return true;
        }
    }
    return false;
}

const std::vector<Diagnostic>& DiagnosticSink::All() const { return diagnostics_; }
void DiagnosticSink::Clear() { diagnostics_.clear(); }

DataType DataType::Void() { return {TypeKind::Void, {}, false}; }
DataType DataType::Bool() { return {TypeKind::Bool, {}, false}; }
DataType DataType::Int() { return {TypeKind::Int, {}, false}; }
DataType DataType::Float() { return {TypeKind::Float, {}, false}; }
DataType DataType::String() { return {TypeKind::String, {}, false}; }
DataType DataType::Object(std::string name, bool handle) {
    return {TypeKind::Object, std::move(name), handle};
}
DataType DataType::Invalid() { return {}; }

std::string DataType::Name() const {
    switch (kind) {
    case TypeKind::Void: return "void";
    case TypeKind::Bool: return "bool";
    case TypeKind::Int: return "int";
    case TypeKind::Float: return "float";
    case TypeKind::String: return "string";
    case TypeKind::Object: return objectName + (isHandle ? "@" : "");
    case TypeKind::Invalid: return "<invalid>";
    }
    return "<invalid>";
}

bool DataType::IsNumeric() const { return kind == TypeKind::Int || kind == TypeKind::Float; }
bool DataType::IsValid() const { return kind != TypeKind::Invalid; }

bool operator==(const DataType& left, const DataType& right) {
    return left.kind == right.kind && left.objectName == right.objectName &&
           left.isHandle == right.isHandle;
}
bool operator!=(const DataType& left, const DataType& right) { return !(left == right); }

Value::Value(bool value) : storage_(value) {}
Value::Value(std::int32_t value) : storage_(value) {}
Value::Value(float value) : storage_(value) {}
Value::Value(std::string value) : storage_(std::move(value)) {}
Value::Value(const char* value) : storage_(std::string(value)) {}

DataType Value::Type() const {
    switch (storage_.index()) {
    case 0: return DataType::Void();
    case 1: return DataType::Bool();
    case 2: return DataType::Int();
    case 3: return DataType::Float();
    case 4: return DataType::String();
    default: return DataType::Invalid();
    }
}

bool Value::IsVoid() const { return std::holds_alternative<std::monostate>(storage_); }
const Value::Storage& Value::Raw() const { return storage_; }

std::string Value::ToString() const {
    if (IsVoid()) return "void";
    if (const auto* value = std::get_if<bool>(&storage_)) return *value ? "true" : "false";
    if (const auto* value = std::get_if<std::int32_t>(&storage_)) return std::to_string(*value);
    if (const auto* value = std::get_if<float>(&storage_)) {
        std::ostringstream stream;
        stream << std::setprecision(7) << *value;
        return stream.str();
    }
    return std::get<std::string>(storage_);
}

bool operator==(const Value& left, const Value& right) { return left.Raw() == right.Raw(); }

} // namespace mini_as

