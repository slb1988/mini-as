#include "mini_as/core.hpp"
#include "mini_as/object.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>
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
DataType DataType::Int8() { return {TypeKind::Int8, {}, false}; }
DataType DataType::Int16() { return {TypeKind::Int16, {}, false}; }
DataType DataType::Int() { return {TypeKind::Int, {}, false}; }
DataType DataType::Int32() { return Int(); }
DataType DataType::Int64() { return {TypeKind::Int64, {}, false}; }
DataType DataType::UInt8() { return {TypeKind::UInt8, {}, false}; }
DataType DataType::UInt16() { return {TypeKind::UInt16, {}, false}; }
DataType DataType::UInt() { return {TypeKind::UInt, {}, false}; }
DataType DataType::UInt32() { return UInt(); }
DataType DataType::UInt64() { return {TypeKind::UInt64, {}, false}; }
DataType DataType::Float() { return {TypeKind::Float, {}, false}; }
DataType DataType::Double() { return {TypeKind::Double, {}, false}; }
DataType DataType::String() { return {TypeKind::String, {}, false}; }
DataType DataType::Enum(std::string name) { return {TypeKind::Enum, std::move(name), false}; }
DataType DataType::Object(std::string name, bool handle) {
    return {TypeKind::Object, std::move(name), handle};
}
DataType DataType::Function(std::string name, bool handle) {
    return {TypeKind::Function, std::move(name), handle};
}
DataType DataType::WeakRef(std::string subtype, bool readOnly) {
    return {readOnly ? TypeKind::ConstWeakRef : TypeKind::WeakRef, std::move(subtype), false};
}
DataType DataType::Invalid() { return {}; }

std::string DataType::Name() const {
    switch (kind) {
    case TypeKind::Void: return "void";
    case TypeKind::Bool: return "bool";
    case TypeKind::Int8: return "int8";
    case TypeKind::Int16: return "int16";
    case TypeKind::Int: return "int";
    case TypeKind::Int64: return "int64";
    case TypeKind::UInt8: return "uint8";
    case TypeKind::UInt16: return "uint16";
    case TypeKind::UInt: return "uint";
    case TypeKind::UInt64: return "uint64";
    case TypeKind::Float: return "float";
    case TypeKind::Double: return "double";
    case TypeKind::String: return "string";
    case TypeKind::Enum: return objectName;
    case TypeKind::Object: return objectName + (isHandle ? "@" : "");
    case TypeKind::Function: return objectName + (isHandle ? "@" : "");
    case TypeKind::WeakRef: return "weakref<" + objectName + ">";
    case TypeKind::ConstWeakRef: return "const_weakref<" + objectName + ">";
    case TypeKind::Invalid: return "<invalid>";
    }
    return "<invalid>";
}

bool DataType::IsNumeric() const {
    return IsInteger() || kind == TypeKind::Float || kind == TypeKind::Double;
}
bool DataType::IsInteger() const { return IsSignedInteger() || IsUnsignedInteger(); }
bool DataType::IsSignedInteger() const {
    return kind == TypeKind::Int8 || kind == TypeKind::Int16 ||
           kind == TypeKind::Int || kind == TypeKind::Int64 || kind == TypeKind::Enum;
}
bool DataType::IsUnsignedInteger() const {
    return kind == TypeKind::UInt8 || kind == TypeKind::UInt16 ||
           kind == TypeKind::UInt || kind == TypeKind::UInt64;
}
unsigned DataType::IntegerBits() const {
    switch (kind) {
    case TypeKind::Int8: case TypeKind::UInt8: return 8;
    case TypeKind::Int16: case TypeKind::UInt16: return 16;
    case TypeKind::Int: case TypeKind::UInt: case TypeKind::Enum: return 32;
    case TypeKind::Int64: case TypeKind::UInt64: return 64;
    default: return 0;
    }
}
bool DataType::IsValid() const { return kind != TypeKind::Invalid; }

bool operator==(const DataType& left, const DataType& right) {
    return left.kind == right.kind && left.objectName == right.objectName &&
           left.isHandle == right.isHandle;
}
bool operator!=(const DataType& left, const DataType& right) { return !(left == right); }

DataType CommonNumericType(const DataType& left, const DataType& right) {
    if (!left.IsNumeric() || !right.IsNumeric()) return DataType::Invalid();
    if (left == DataType::Double() || right == DataType::Double()) return DataType::Double();
    if (left == DataType::Float() || right == DataType::Float()) return DataType::Float();
    DataType promotedLeft = left.IntegerBits() < 32 ? DataType::Int() : left;
    DataType promotedRight = right.IntegerBits() < 32 ? DataType::Int() : right;
    if (promotedLeft == promotedRight) return promotedLeft;
    const unsigned width = std::max(promotedLeft.IntegerBits(), promotedRight.IntegerBits());
    const bool unsignedResult =
        (promotedLeft.IsUnsignedInteger() && promotedLeft.IntegerBits() >= promotedRight.IntegerBits()) ||
        (promotedRight.IsUnsignedInteger() && promotedRight.IntegerBits() >= promotedLeft.IntegerBits());
    if (width == 64) return unsignedResult ? DataType::UInt64() : DataType::Int64();
    return unsignedResult ? DataType::UInt() : DataType::Int();
}

bool operator==(const IntegerStorage& left, const IntegerStorage& right) {
    return left.kind == right.kind && left.bits == right.bits && left.typeName == right.typeName;
}

bool operator==(const HostValueStorage& left, const HostValueStorage& right) {
    return left.typeName == right.typeName && left.value.type() == right.value.type() &&
           !left.value.has_value() && !right.value.has_value();
}

namespace {

void EnumerateValueReferences(const Value& value, const ReferenceVisitor& visitor,
                              std::unordered_set<const CapturedCell*>& visited) {
    if (const auto* object = std::get_if<ObjectHandle>(&value.Raw())) {
        if (*object) visitor(object->Get());
        return;
    }
    if (const auto* function = std::get_if<FunctionHandle>(&value.Raw())) {
        if (function->object) visitor(function->object.Get());
        for (const auto& cell : function->captures)
            if (cell && visited.insert(cell.get()).second)
                EnumerateValueReferences(cell->value, visitor, visited);
        return;
    }
    if (const auto* cell = std::get_if<CapturedCellHandle>(&value.Raw())) {
        if (*cell && visited.insert(cell->get()).second)
            EnumerateValueReferences((*cell)->value, visitor, visited);
        return;
    }
    if (const auto* host = std::get_if<HostValueStorage>(&value.Raw())) {
        if (host->enumerateReferences) host->enumerateReferences(host->value, visitor);
        return;
    }
    if (const auto* reference = std::get_if<ReferenceStorage>(&value.Raw()))
        if (reference->object) visitor(reference->object.Get());
}

} // namespace

bool operator==(const ReferenceStorage& left, const ReferenceStorage& right) {
    return left.kind == right.kind && left.type == right.type && left.slot == right.slot &&
           left.object == right.object;
}

bool operator==(const FunctionHandle& left, const FunctionHandle& right) {
    return left.function == right.function && left.signature == right.signature &&
           left.typeName == right.typeName && left.host == right.host &&
           left.object == right.object && left.dispatchType == right.dispatchType &&
           left.virtualSlot == right.virtualSlot && left.virtualMethod == right.virtualMethod &&
           left.captures == right.captures;
}

Value::Value(bool value) : storage_(value) {}
Value::Value(std::int32_t value) : storage_(value) {}
Value::Value(float value) : storage_(value) {}
Value::Value(double value) : storage_(value) {}
Value::Value(std::string value) : storage_(std::move(value)) {}
Value::Value(const char* value) : storage_(std::string(value)) {}
Value::Value(ObjectHandle value) : storage_(std::move(value)) {}
Value::Value(FunctionHandle value) : storage_(std::move(value)) {}
Value::Value(WeakObjectHandle value) : storage_(std::move(value)) {}
Value::Value(CapturedCellHandle value) : storage_(std::move(value)) {}
Value::Value(ReferenceStorage value) : storage_(std::move(value)) {}

Value Value::Integer(const DataType& type, std::uint64_t bits) {
    if (!type.IsInteger()) throw std::runtime_error("integer value requires an integer type");
    const unsigned width = type.IntegerBits();
    if (width < 64) bits &= (std::uint64_t{1} << width) - 1;
    if (type == DataType::Int()) return Value(static_cast<std::int32_t>(static_cast<std::uint32_t>(bits)));
    Value value;
    value.storage_ = IntegerStorage{type.kind, bits, type.objectName};
    return value;
}

DataType Value::Type() const {
    switch (storage_.index()) {
    case 0: return DataType::Void();
    case 1: return DataType::Bool();
    case 2: return DataType::Int();
    case 3: {
        const auto& integer = std::get<IntegerStorage>(storage_);
        return {integer.kind, integer.typeName, false};
    }
    case 4: return DataType::Float();
    case 5: return DataType::Double();
    case 6: return DataType::String();
    case 7: {
        const auto& handle = std::get<ObjectHandle>(storage_);
        return handle ? DataType::Object(handle.Get()->GetTypeInfo()->name, true)
                      : DataType::Object("<null>", true);
    }
    case 8: {
        const auto& handle = std::get<FunctionHandle>(storage_);
        return DataType::Function(handle.typeName, true);
    }
    case 9: {
        const auto& handle = std::get<WeakObjectHandle>(storage_);
        return DataType::WeakRef(handle.TypeName(), handle.IsReadOnly());
    }
    case 10: {
        const auto& cell = std::get<CapturedCellHandle>(storage_);
        return cell ? cell->value.Type() : DataType::Invalid();
    }
    case 11: return DataType::Object(std::get<HostValueStorage>(storage_).typeName, false);
    case 12: return std::get<ReferenceStorage>(storage_).type;
    default: return DataType::Invalid();
    }
}

bool Value::IsVoid() const { return std::holds_alternative<std::monostate>(storage_); }
bool Value::IsReference() const { return std::holds_alternative<ReferenceStorage>(storage_); }
std::uint64_t Value::UnsignedInteger() const {
    if (const auto* value = std::get_if<std::int32_t>(&storage_))
        return static_cast<std::uint32_t>(*value);
    if (const auto* value = std::get_if<IntegerStorage>(&storage_)) return value->bits;
    throw std::runtime_error("value is not an integer");
}
std::int64_t Value::SignedInteger() const {
    if (const auto* value = std::get_if<std::int32_t>(&storage_)) return *value;
    const auto* value = std::get_if<IntegerStorage>(&storage_);
    if (!value || !Type().IsSignedInteger()) throw std::runtime_error("value is not a signed integer");
    const unsigned width = Type().IntegerBits();
    if (width == 64) return static_cast<std::int64_t>(value->bits);
    const std::uint64_t sign = std::uint64_t{1} << (width - 1);
    return static_cast<std::int64_t>((value->bits ^ sign) - sign);
}
const Value::Storage& Value::Raw() const { return storage_; }

void Value::EnumerateReferences(const ReferenceVisitor& visitor) const {
    std::unordered_set<const CapturedCell*> visited;
    EnumerateValueReferences(*this, visitor, visited);
}

void Value::ClearReferences() {
    if (auto* object = std::get_if<ObjectHandle>(&storage_)) {
        *object = {};
        return;
    }
    if (auto* function = std::get_if<FunctionHandle>(&storage_)) {
        function->object = {};
        function->captures.clear();
        return;
    }
    if (auto* cell = std::get_if<CapturedCellHandle>(&storage_)) {
        if (*cell) (*cell)->value.ClearReferences();
        cell->reset();
        return;
    }
    if (auto* host = std::get_if<HostValueStorage>(&storage_)) {
        if (host->clearReferences) host->clearReferences(host->value);
        return;
    }
    if (auto* reference = std::get_if<ReferenceStorage>(&storage_))
        reference->object = {};
}

std::string Value::ToString() const {
    if (IsVoid()) return "void";
    if (const auto* value = std::get_if<bool>(&storage_)) return *value ? "true" : "false";
    if (const auto* value = std::get_if<std::int32_t>(&storage_)) return std::to_string(*value);
    if (std::holds_alternative<IntegerStorage>(storage_)) {
        return Type().IsSignedInteger() ? std::to_string(SignedInteger())
                                        : std::to_string(UnsignedInteger());
    }
    if (const auto* value = std::get_if<float>(&storage_)) {
        std::ostringstream stream;
        stream << std::setprecision(7) << *value;
        return stream.str();
    }
    if (const auto* value = std::get_if<double>(&storage_)) {
        std::ostringstream stream;
        stream << std::setprecision(15) << *value;
        return stream.str();
    }
    if (const auto* value = std::get_if<std::string>(&storage_)) return *value;
    if (std::holds_alternative<ReferenceStorage>(storage_)) return "<reference>";
    if (const auto* weak = std::get_if<WeakObjectHandle>(&storage_))
        return weak->Expired() ? "null" : "<weakref<" + weak->TypeName() + ">>";
    if (const auto* cell = std::get_if<CapturedCellHandle>(&storage_))
        return *cell ? (*cell)->value.ToString() : "<capture>";
    if (const auto* handle = std::get_if<FunctionHandle>(&storage_))
        return *handle ? "<" + handle->typeName + "@>" : "null";
    if (const auto* value = std::get_if<HostValueStorage>(&storage_))
        return "<" + value->typeName + ">";
    const auto& handle = std::get<ObjectHandle>(storage_);
    return handle ? "<" + handle.Get()->GetTypeInfo()->name + "@>" : "null";
}

bool operator==(const Value& left, const Value& right) { return left.Raw() == right.Raw(); }

Value ConvertInteger(const Value& value, const DataType& target) {
    if (!target.IsInteger()) throw std::runtime_error("integer conversion requires integer target");
    std::uint64_t bits = 0;
    if (value.Type().IsInteger()) {
        bits = value.Type().IsSignedInteger()
            ? static_cast<std::uint64_t>(value.SignedInteger()) : value.UnsignedInteger();
    } else if (value.Type() == DataType::Float()) {
        bits = target.IsSignedInteger()
            ? static_cast<std::uint64_t>(static_cast<std::int64_t>(value.As<float>()))
            : static_cast<std::uint64_t>(value.As<float>());
    } else if (value.Type() == DataType::Double()) {
        bits = target.IsSignedInteger()
            ? static_cast<std::uint64_t>(static_cast<std::int64_t>(value.As<double>()))
            : static_cast<std::uint64_t>(value.As<double>());
    } else {
        throw std::runtime_error("integer conversion requires numeric source");
    }
    return Value::Integer(target, bits);
}

} // namespace mini_as
