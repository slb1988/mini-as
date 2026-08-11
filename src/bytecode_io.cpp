#include "bytecode_io.hpp"

#include <cstring>
#include <iterator>
#include <limits>
#include <ostream>
#include <istream>
#include <type_traits>

namespace mini_as::detail {
namespace {

constexpr char kMagic[] = {'M', 'A', 'S', 'B'};
constexpr std::uint32_t kVersion = 7;
constexpr std::uint64_t kMaxItems = 1'000'000;
constexpr std::uint64_t kMaxString = 16 * 1024 * 1024;

template <typename T, bool IsEnum = std::is_enum_v<T>>
struct ScalarStorage { using type = T; };

template <typename T>
struct ScalarStorage<T, true> { using type = std::underlying_type_t<T>; };

template <typename T>
using ScalarStorageT = typename ScalarStorage<T>::type;

std::uint64_t Checksum(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

class Writer {
public:
    template <typename T>
    void Scalar(T value) {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
        using Stored = ScalarStorageT<T>;
        using Unsigned = std::make_unsigned_t<Stored>;
        Unsigned bits = static_cast<Unsigned>(static_cast<Stored>(value));
        for (std::size_t byte = 0; byte < sizeof(Stored); ++byte)
            data_.push_back(static_cast<std::uint8_t>(bits >> (byte * 8)));
    }

    void Float(float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        Scalar(bits);
    }

    void Double(double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        Scalar(bits);
    }

    void String(std::string_view value) {
        Scalar<std::uint64_t>(value.size());
        data_.insert(data_.end(), value.begin(), value.end());
    }

    template <typename T, typename Callback>
    void Vector(const std::vector<T>& values, Callback callback) {
        Scalar<std::uint64_t>(values.size());
        for (const auto& value : values) callback(*this, value);
    }

    const std::vector<std::uint8_t>& Data() const { return data_; }

private:
    std::vector<std::uint8_t> data_;
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    template <typename T>
    bool Scalar(T& value) {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
        using Stored = ScalarStorageT<T>;
        using Unsigned = std::make_unsigned_t<Stored>;
        if (Remaining() < sizeof(Stored)) return Fail("truncated scalar");
        Unsigned bits = 0;
        for (std::size_t byte = 0; byte < sizeof(Stored); ++byte)
            bits |= static_cast<Unsigned>(data_[position_++]) << (byte * 8);
        value = static_cast<T>(static_cast<Stored>(bits));
        return true;
    }

    bool Float(float& value) {
        std::uint32_t bits = 0;
        if (!Scalar(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool Double(double& value) {
        std::uint64_t bits = 0;
        if (!Scalar(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool String(std::string& value) {
        std::uint64_t size = 0;
        if (!Scalar(size)) return false;
        if (size > kMaxString || size > Remaining()) return Fail("invalid string length");
        value.assign(reinterpret_cast<const char*>(data_ + position_),
                     static_cast<std::size_t>(size));
        position_ += static_cast<std::size_t>(size);
        return true;
    }

    template <typename T, typename Callback>
    bool Vector(std::vector<T>& values, Callback callback) {
        std::uint64_t size = 0;
        if (!Scalar(size)) return false;
        if (size > kMaxItems) return Fail("collection is too large");
        values.clear();
        values.reserve(static_cast<std::size_t>(size));
        for (std::uint64_t index = 0; index < size; ++index) {
            T value{};
            if (!callback(*this, value)) return false;
            values.push_back(std::move(value));
        }
        return true;
    }

    bool Done() const { return position_ == size_; }
    const std::string& Error() const { return error_; }

private:
    std::size_t Remaining() const { return size_ - position_; }
    bool Fail(std::string error) {
        if (error_.empty()) error_ = std::move(error);
        return false;
    }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    std::string error_;
};

void WriteId(Writer& writer, FunctionId id) { writer.Scalar(id.value); }
void WriteId(Writer& writer, TypeId id) { writer.Scalar(id.value); }
void WriteId(Writer& writer, GlobalId id) { writer.Scalar(id.value); }
void WriteId(Writer& writer, VariableId id) { writer.Scalar(id.value); }
bool ReadId(Reader& reader, FunctionId& id) { return reader.Scalar(id.value); }
bool ReadId(Reader& reader, TypeId& id) { return reader.Scalar(id.value); }
bool ReadId(Reader& reader, GlobalId& id) { return reader.Scalar(id.value); }
bool ReadId(Reader& reader, VariableId& id) { return reader.Scalar(id.value); }

void WriteType(Writer& writer, const DataType& type) {
    writer.Scalar(type.kind);
    writer.String(type.objectName);
    writer.Scalar<std::uint8_t>(type.isHandle ? 1 : 0);
}

bool ReadType(Reader& reader, DataType& type) {
    std::uint8_t handle = 0;
    if (!reader.Scalar(type.kind) || type.kind > TypeKind::Invalid ||
        !reader.String(type.objectName) || !reader.Scalar(handle) || handle > 1) return false;
    type.isHandle = handle != 0;
    return true;
}

void WriteLocation(Writer& writer, const SourceLocation& location) {
    writer.String(location.section);
    writer.Scalar<std::uint64_t>(location.offset);
    writer.Scalar<std::int32_t>(location.row);
    writer.Scalar<std::int32_t>(location.column);
}

bool ReadLocation(Reader& reader, SourceLocation& location) {
    std::uint64_t offset = 0;
    std::int32_t row = 0, column = 0;
    if (!reader.String(location.section) || !reader.Scalar(offset) ||
        !reader.Scalar(row) || !reader.Scalar(column)) return false;
    if (offset > std::numeric_limits<std::size_t>::max()) return false;
    location.offset = static_cast<std::size_t>(offset);
    location.row = row;
    location.column = column;
    return true;
}

void WriteFunctionSignature(Writer& writer, const FunctionSignature& signature) {
    writer.String(signature.name);
    WriteType(writer, signature.returnType);
    writer.Vector(signature.parameters, [](Writer& out, const DataType& type) { WriteType(out, type); });
    writer.Scalar<std::uint8_t>(signature.host ? 1 : 0);
    WriteId(writer, signature.id);
    writer.String(signature.objectType);
    writer.Scalar<std::uint8_t>(signature.method ? 1 : 0);
    writer.Scalar<std::uint8_t>(signature.constructor ? 1 : 0);
    writer.Scalar<std::uint64_t>(signature.defaultArgumentCount);
    writer.Vector(signature.parameterNames,
        [](Writer& out, const std::string& value) { out.String(value); });
    writer.Vector(signature.parameterModes,
        [](Writer& out, ParameterMode mode) { out.Scalar(mode); });
    writer.Scalar<std::uint8_t>(signature.returnsReference ? 1 : 0);
    writer.Scalar<std::uint8_t>(signature.returnReferenceConst ? 1 : 0);
    writer.Scalar<std::uint8_t>(signature.destructor ? 1 : 0);
    writer.Scalar(signature.access);
    writer.Scalar<std::uint8_t>(signature.propertyAccessor ? 1 : 0);
    writer.Scalar<std::uint8_t>(signature.factory ? 1 : 0);
    writer.Scalar<std::uint8_t>(signature.readOnlyMethod ? 1 : 0);
    writer.Scalar<std::uint8_t>(signature.imported ? 1 : 0);
    writer.String(signature.sourceModule);
    writer.Scalar<std::uint8_t>(signature.shared ? 1 : 0);
    writer.Scalar<std::uint8_t>(signature.external ? 1 : 0);
    writer.Scalar<std::uint8_t>(signature.variadic ? 1 : 0);
}

bool ReadBool(Reader& reader, bool& value) {
    std::uint8_t stored = 0;
    if (!reader.Scalar(stored) || stored > 1) return false;
    value = stored != 0;
    return true;
}

bool ReadFunctionSignature(Reader& reader, FunctionSignature& signature) {
    std::uint64_t defaults = 0;
    if (!reader.String(signature.name) || !ReadType(reader, signature.returnType) ||
        !reader.Vector(signature.parameters,
            [](Reader& in, DataType& type) { return ReadType(in, type); }) ||
        !ReadBool(reader, signature.host) || !ReadId(reader, signature.id) ||
        !reader.String(signature.objectType) || !ReadBool(reader, signature.method) ||
        !ReadBool(reader, signature.constructor) || !reader.Scalar(defaults) ||
        defaults > signature.parameters.size() ||
        !reader.Vector(signature.parameterNames,
            [](Reader& in, std::string& value) { return in.String(value); }) ||
        !reader.Vector(signature.parameterModes,
            [](Reader& in, ParameterMode& mode) {
                return in.Scalar(mode) && mode <= ParameterMode::InOut;
            }) ||
        !ReadBool(reader, signature.returnsReference) ||
        !ReadBool(reader, signature.returnReferenceConst) ||
        !ReadBool(reader, signature.destructor) || !reader.Scalar(signature.access) ||
        signature.access > MemberAccess::Private ||
        !ReadBool(reader, signature.propertyAccessor) ||
        !ReadBool(reader, signature.factory) ||
        !ReadBool(reader, signature.readOnlyMethod) ||
        !ReadBool(reader, signature.imported) ||
        !reader.String(signature.sourceModule) ||
        !ReadBool(reader, signature.shared) ||
        !ReadBool(reader, signature.external) ||
        !ReadBool(reader, signature.variadic)) return false;
    signature.defaultArgumentCount = static_cast<std::size_t>(defaults);
    return signature.parameterNames.size() <= signature.parameters.size() &&
           signature.parameterModes.size() <= signature.parameters.size() &&
           (!signature.variadic ||
            (!signature.parameters.empty() && signature.defaultArgumentCount == 0));
}

void WriteValue(Writer& writer, const Value& value, bool& valid) {
    const auto& raw = value.Raw();
    writer.Scalar<std::uint8_t>(static_cast<std::uint8_t>(raw.index()));
    switch (raw.index()) {
    case 0: break;
    case 1: writer.Scalar<std::uint8_t>(std::get<bool>(raw) ? 1 : 0); break;
    case 2: writer.Scalar(std::get<std::int32_t>(raw)); break;
    case 3: {
        const auto& integer = std::get<IntegerStorage>(raw);
        writer.Scalar(integer.kind);
        writer.Scalar(integer.bits);
        writer.String(integer.typeName);
        break;
    }
    case 4: writer.Float(std::get<float>(raw)); break;
    case 5: writer.Double(std::get<double>(raw)); break;
    case 6: writer.String(std::get<std::string>(raw)); break;
    case 7:
        if (std::get<ObjectHandle>(raw)) valid = false;
        break;
    case 8: {
        const auto& handle = std::get<FunctionHandle>(raw);
        if (handle.object || !handle.captures.empty()) { valid = false; break; }
        WriteId(writer, handle.function);
        WriteId(writer, handle.signature);
        writer.String(handle.typeName);
        writer.Scalar<std::uint8_t>(handle.host ? 1 : 0);
        WriteId(writer, handle.dispatchType);
        writer.Scalar(handle.virtualSlot);
        writer.Scalar<std::uint8_t>(handle.virtualMethod ? 1 : 0);
        break;
    }
    case 9: {
        const auto& weak = std::get<WeakObjectHandle>(raw);
        if (!weak.Expired()) { valid = false; break; }
        writer.String(weak.TypeName());
        writer.Scalar<std::uint8_t>(weak.IsReadOnly() ? 1 : 0);
        break;
    }
    case 10:
        if (std::get<CapturedCellHandle>(raw)) valid = false;
        break;
    case 11:
        writer.String(std::get<HostValueStorage>(raw).typeName);
        break;
    case 12: {
        const auto& reference = std::get<ReferenceStorage>(raw);
        if (reference.object) { valid = false; break; }
        writer.Scalar(reference.kind);
        WriteType(writer, reference.type);
        writer.Scalar(reference.slot);
        break;
    }
    default: valid = false; break;
    }
}

bool ReadValue(Reader& reader, Value& value) {
    std::uint8_t tag = 0;
    if (!reader.Scalar(tag) || tag > 12) return false;
    switch (tag) {
    case 0: value = Value{}; return true;
    case 1: { bool stored = false; if (!ReadBool(reader, stored)) return false; value = Value(stored); return true; }
    case 2: { std::int32_t stored = 0; if (!reader.Scalar(stored)) return false; value = Value(stored); return true; }
    case 3: {
        TypeKind kind{}; std::uint64_t bits = 0; std::string name;
        if (!reader.Scalar(kind) || kind > TypeKind::Invalid ||
            !reader.Scalar(bits) || !reader.String(name)) return false;
        DataType type{kind, name, false};
        value = Value::Integer(type, bits);
        return true;
    }
    case 4: { float stored = 0; if (!reader.Float(stored)) return false; value = Value(stored); return true; }
    case 5: { double stored = 0; if (!reader.Double(stored)) return false; value = Value(stored); return true; }
    case 6: { std::string stored; if (!reader.String(stored)) return false; value = Value(std::move(stored)); return true; }
    case 7: value = Value(ObjectHandle{}); return true;
    case 8: {
        FunctionHandle handle;
        if (!ReadId(reader, handle.function) || !ReadId(reader, handle.signature) ||
            !reader.String(handle.typeName) || !ReadBool(reader, handle.host) ||
            !ReadId(reader, handle.dispatchType) || !reader.Scalar(handle.virtualSlot) ||
            !ReadBool(reader, handle.virtualMethod)) return false;
        value = Value(std::move(handle));
        return true;
    }
    case 9: {
        std::string name; bool readOnly = false;
        if (!reader.String(name) || !ReadBool(reader, readOnly)) return false;
        value = Value(WeakObjectHandle(std::move(name), readOnly));
        return true;
    }
    case 10: value = Value(CapturedCellHandle{}); return true;
    case 11: {
        std::string name; if (!reader.String(name)) return false;
        value = Value::HostValue(std::move(name), std::monostate{});
        return true;
    }
    case 12: {
        ReferenceStorage reference;
        if (!reader.Scalar(reference.kind) || reference.kind > ReferenceKind::Field ||
            !ReadType(reader, reference.type) || !reader.Scalar(reference.slot)) return false;
        value = Value(std::move(reference));
        return true;
    }
    }
    return false;
}

void WriteField(Writer& writer, const FieldSignature& field) {
    writer.String(field.name); WriteType(writer, field.type); writer.String(field.objectType);
    writer.Scalar(field.access); writer.Scalar<std::uint8_t>(field.isConst ? 1 : 0);
    writer.Scalar<std::uint8_t>(field.host ? 1 : 0);
}

bool ReadField(Reader& reader, FieldSignature& field) {
    return reader.String(field.name) && ReadType(reader, field.type) &&
        reader.String(field.objectType) && reader.Scalar(field.access) &&
        field.access <= MemberAccess::Private && ReadBool(reader, field.isConst) &&
        ReadBool(reader, field.host);
}

void WriteClass(Writer& writer, const ClassSignature& type, bool& valid) {
    writer.String(type.name); writer.Scalar<std::uint8_t>(type.interfaceType ? 1 : 0);
    writer.Vector(type.inheritedTypes, [](Writer& out, const std::string& value) { out.String(value); });
    writer.String(type.baseClass);
    writer.Vector(type.interfaces, [](Writer& out, const std::string& value) { out.String(value); });
    writer.Vector(type.fields, [](Writer& out, const FieldSignature& field) { WriteField(out, field); });
    writer.Scalar<std::uint64_t>(type.inheritedFieldCount);
    writer.Vector(type.methods, [](Writer& out, const FunctionSignature& method) { WriteFunctionSignature(out, method); });
    writer.Scalar<std::uint8_t>(type.defaultConstructorDeleted ? 1 : 0);
    writer.Scalar<std::uint8_t>(type.defaultCopyConstructorDeleted ? 1 : 0);
    writer.Scalar<std::uint8_t>(type.defaultCopyAssignmentDeleted ? 1 : 0);
    writer.Scalar<std::uint8_t>(type.generatedCopyConstructor ? 1 : 0);
    WriteId(writer, type.id); writer.Scalar<std::uint8_t>(type.host ? 1 : 0);
    writer.Scalar<std::uint8_t>(type.valueType ? 1 : 0);
    WriteValue(writer, type.defaultValue, valid);
    writer.Scalar<std::uint8_t>(type.shared ? 1 : 0);
}

bool ReadClass(Reader& reader, ClassSignature& type) {
    std::uint64_t inheritedFields = 0;
    if (!reader.String(type.name) || !ReadBool(reader, type.interfaceType) ||
        !reader.Vector(type.inheritedTypes, [](Reader& in, std::string& value) { return in.String(value); }) ||
        !reader.String(type.baseClass) ||
        !reader.Vector(type.interfaces, [](Reader& in, std::string& value) { return in.String(value); }) ||
        !reader.Vector(type.fields, [](Reader& in, FieldSignature& field) { return ReadField(in, field); }) ||
        !reader.Scalar(inheritedFields) || inheritedFields > type.fields.size() ||
        !reader.Vector(type.methods, [](Reader& in, FunctionSignature& method) { return ReadFunctionSignature(in, method); }) ||
        !ReadBool(reader, type.defaultConstructorDeleted) ||
        !ReadBool(reader, type.defaultCopyConstructorDeleted) ||
        !ReadBool(reader, type.defaultCopyAssignmentDeleted) ||
        !ReadBool(reader, type.generatedCopyConstructor) || !ReadId(reader, type.id) ||
        !ReadBool(reader, type.host) || !ReadBool(reader, type.valueType) ||
        !ReadValue(reader, type.defaultValue) || !ReadBool(reader, type.shared)) return false;
    type.inheritedFieldCount = static_cast<std::size_t>(inheritedFields);
    return true;
}

void WriteGlobal(Writer& writer, const GlobalSignature& global) {
    writer.String(global.name); WriteType(writer, global.type);
    writer.Scalar<std::uint8_t>(global.isConst ? 1 : 0); WriteId(writer, global.id);
    writer.Scalar<std::uint8_t>(global.host ? 1 : 0);
}

bool ReadGlobal(Reader& reader, GlobalSignature& global) {
    return reader.String(global.name) && ReadType(reader, global.type) &&
        ReadBool(reader, global.isConst) && ReadId(reader, global.id) &&
        ReadBool(reader, global.host);
}

void WriteEnum(Writer& writer, const EnumSignature& type) {
    writer.String(type.name);
    writer.Vector(type.values, [](Writer& out, const EnumValueSignature& value) {
        out.String(value.name); out.Scalar(value.value);
    });
    WriteId(writer, type.id);
    writer.Scalar<std::uint8_t>(type.shared ? 1 : 0);
}

bool ReadEnum(Reader& reader, EnumSignature& type) {
    return reader.String(type.name) &&
        reader.Vector(type.values, [](Reader& in, EnumValueSignature& value) {
            return in.String(value.name) && in.Scalar(value.value);
        }) && ReadId(reader, type.id) && ReadBool(reader, type.shared);
}

void WriteTypedef(Writer& writer, const TypedefSignature& type) {
    writer.String(type.name); WriteType(writer, type.underlyingType); WriteId(writer, type.id);
}
bool ReadTypedef(Reader& reader, TypedefSignature& type) {
    return reader.String(type.name) && ReadType(reader, type.underlyingType) && ReadId(reader, type.id);
}
void WriteFuncdef(Writer& writer, const FuncdefSignature& type) {
    writer.String(type.name); WriteFunctionSignature(writer, type.signature);
    WriteId(writer, type.id); writer.String(type.parentType);
    writer.Scalar<std::uint8_t>(type.shared ? 1 : 0);
}
bool ReadFuncdef(Reader& reader, FuncdefSignature& type) {
    return reader.String(type.name) && ReadFunctionSignature(reader, type.signature) &&
        ReadId(reader, type.id) && reader.String(type.parentType) &&
        ReadBool(reader, type.shared);
}

void WriteInstruction(Writer& writer, const Instruction& instruction) {
    writer.Scalar(instruction.opcode); writer.Scalar(instruction.operand);
    WriteLocation(writer, instruction.location);
}
bool ReadInstruction(Reader& reader, Instruction& instruction) {
    return reader.Scalar(instruction.opcode) && instruction.opcode <= OpCode::Return &&
        reader.Scalar(instruction.operand) && ReadLocation(reader, instruction.location);
}

void WriteDebugVariable(Writer& writer, const LocalVariableDebugInfo& variable) {
    writer.String(variable.name); WriteType(writer, variable.type); WriteId(writer, variable.slot);
    writer.Scalar<std::uint8_t>(variable.isConst ? 1 : 0);
    writer.Scalar<std::uint8_t>(variable.parameter ? 1 : 0);
    writer.Scalar<std::uint64_t>(variable.scopeBegin);
    writer.Scalar<std::uint64_t>(variable.scopeEnd);
}

bool ReadDebugVariable(Reader& reader, LocalVariableDebugInfo& variable) {
    std::uint64_t begin = 0, end = 0;
    std::uint8_t isConst = 0, parameter = 0;
    if (!reader.String(variable.name) || !ReadType(reader, variable.type) ||
        !ReadId(reader, variable.slot) || !reader.Scalar(isConst) || isConst > 1 ||
        !reader.Scalar(parameter) || parameter > 1 ||
        !reader.Scalar(begin) || !reader.Scalar(end) ||
        begin > std::numeric_limits<std::size_t>::max() ||
        end > std::numeric_limits<std::size_t>::max()) return false;
    variable.isConst = isConst != 0;
    variable.parameter = parameter != 0;
    variable.scopeBegin = static_cast<std::size_t>(begin);
    variable.scopeEnd = static_cast<std::size_t>(end);
    return true;
}

void WriteFunction(Writer& writer, const BytecodeFunction& function, bool& valid) {
    WriteFunctionSignature(writer, function.signature);
    writer.Vector(function.code, [](Writer& out, const Instruction& instruction) { WriteInstruction(out, instruction); });
    writer.Scalar<std::uint64_t>(function.constants.size());
    for (const auto& constant : function.constants) WriteValue(writer, constant, valid);
    writer.Vector(function.exceptionHandlers, [](Writer& out, const ExceptionHandler& handler) {
        out.Scalar<std::uint64_t>(handler.tryBegin); out.Scalar<std::uint64_t>(handler.tryEnd);
        out.Scalar<std::uint64_t>(handler.catchTarget);
    });
    writer.Vector(function.debugVariables, [](Writer& out, const LocalVariableDebugInfo& variable) {
        WriteDebugVariable(out, variable);
    });
    writer.Scalar<std::uint64_t>(function.localCount);
}

bool ReadFunction(Reader& reader, BytecodeFunction& function) {
    std::uint64_t constants = 0, locals = 0;
    if (!ReadFunctionSignature(reader, function.signature) ||
        !reader.Vector(function.code, [](Reader& in, Instruction& instruction) { return ReadInstruction(in, instruction); }) ||
        !reader.Scalar(constants) || constants > kMaxItems) return false;
    function.constants.clear(); function.constants.reserve(static_cast<std::size_t>(constants));
    for (std::uint64_t index = 0; index < constants; ++index) {
        Value value; if (!ReadValue(reader, value)) return false;
        function.constants.push_back(std::move(value));
    }
    if (!reader.Vector(function.exceptionHandlers, [](Reader& in, ExceptionHandler& handler) {
            std::uint64_t begin = 0, end = 0, target = 0;
            if (!in.Scalar(begin) || !in.Scalar(end) || !in.Scalar(target) ||
                begin > std::numeric_limits<std::size_t>::max() ||
                end > std::numeric_limits<std::size_t>::max() ||
                target > std::numeric_limits<std::size_t>::max()) return false;
            handler.tryBegin = static_cast<std::size_t>(begin);
            handler.tryEnd = static_cast<std::size_t>(end);
            handler.catchTarget = static_cast<std::size_t>(target);
            return true;
        }) || !reader.Vector(function.debugVariables,
            [](Reader& in, LocalVariableDebugInfo& variable) {
                return ReadDebugVariable(in, variable);
            }) || !reader.Scalar(locals) ||
        locals > std::numeric_limits<std::size_t>::max()) return false;
    function.localCount = static_cast<std::size_t>(locals);
    for (const auto& variable : function.debugVariables)
        if (!variable.slot.IsValid() || variable.slot.value >= function.localCount ||
            variable.scopeBegin > variable.scopeEnd ||
            variable.scopeEnd > function.code.size()) return false;
    for (const auto& instruction : function.code) {
        if ((instruction.opcode == OpCode::PushConst &&
             (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) >= function.constants.size())) ||
            ((instruction.opcode == OpCode::Jump || instruction.opcode == OpCode::JumpIfFalse) &&
             (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) > function.code.size()))) return false;
    }
    return true;
}

void WriteModule(Writer& writer, const BytecodeModule& module, bool& valid) {
    writer.Vector(module.functions, [&valid](Writer& out, const BytecodeFunction& function) { WriteFunction(out, function, valid); });
    WriteFunction(writer, module.globalInitializer, valid);
    writer.Vector(module.globals, [](Writer& out, const GlobalBinding& global) { WriteGlobal(out, global.signature); });
    writer.Vector(module.callables, [](Writer& out, const CallableRef& callable) {
        out.Scalar(callable.kind); WriteId(out, callable.function); WriteId(out, callable.objectType);
        out.Scalar(callable.virtualSlot); out.Scalar(callable.parameterCount); WriteId(out, callable.signatureType);
        out.Vector(callable.argumentTypes,
            [](Writer& nested, const DataType& type) { WriteType(nested, type); });
    });
    writer.Vector(module.virtualDispatch, [](Writer& out, const VirtualDispatchEntry& entry) {
        WriteId(out, entry.concreteType); WriteId(out, entry.interfaceType);
        out.Scalar(entry.slot); WriteId(out, entry.implementation);
    });
    writer.Vector(module.destructors, [](Writer& out, const std::pair<TypeId, FunctionId>& entry) {
        WriteId(out, entry.first); WriteId(out, entry.second);
    });
    writer.Vector(module.funcdefs, [](Writer& out, const FuncdefSignature& type) { WriteFuncdef(out, type); });
    writer.Vector(module.imports, [](Writer& out, const ImportedFunction& imported) {
        WriteFunctionSignature(out, imported.signature);
        out.String(imported.sourceModule);
    });
}

bool ReadModule(Reader& reader, BytecodeModule& module) {
    return reader.Vector(module.functions, [](Reader& in, BytecodeFunction& function) { return ReadFunction(in, function); }) &&
        ReadFunction(reader, module.globalInitializer) &&
        reader.Vector(module.globals, [](Reader& in, GlobalBinding& global) { return ReadGlobal(in, global.signature); }) &&
        reader.Vector(module.callables, [](Reader& in, CallableRef& callable) {
            return in.Scalar(callable.kind) && callable.kind <= CallableKind::ExternalFunction &&
                ReadId(in, callable.function) && ReadId(in, callable.objectType) &&
                in.Scalar(callable.virtualSlot) && in.Scalar(callable.parameterCount) &&
                ReadId(in, callable.signatureType) &&
                in.Vector(callable.argumentTypes,
                    [](Reader& nested, DataType& type) { return ReadType(nested, type); }) &&
                (callable.argumentTypes.empty() ||
                 callable.argumentTypes.size() == callable.parameterCount);
        }) &&
        reader.Vector(module.virtualDispatch, [](Reader& in, VirtualDispatchEntry& entry) {
            return ReadId(in, entry.concreteType) && ReadId(in, entry.interfaceType) &&
                in.Scalar(entry.slot) && ReadId(in, entry.implementation);
        }) &&
        reader.Vector(module.destructors, [](Reader& in, std::pair<TypeId, FunctionId>& entry) {
            return ReadId(in, entry.first) && ReadId(in, entry.second);
        }) &&
        reader.Vector(module.funcdefs, [](Reader& in, FuncdefSignature& type) { return ReadFuncdef(in, type); }) &&
        reader.Vector(module.imports, [](Reader& in, ImportedFunction& imported) {
            return ReadFunctionSignature(in, imported.signature) &&
                   in.String(imported.sourceModule);
        });
}

void WriteEnvironment(Writer& writer, const ModuleCompilationEnvironment& environment, bool& valid) {
    writer.Vector(environment.functions, [](Writer& out, const FunctionSignature& function) { WriteFunctionSignature(out, function); });
    writer.Scalar<std::uint64_t>(environment.classes.size());
    for (const auto& type : environment.classes) WriteClass(writer, type, valid);
    writer.Vector(environment.globals, [](Writer& out, const GlobalSignature& global) { WriteGlobal(out, global); });
    writer.Vector(environment.enums, [](Writer& out, const EnumSignature& type) { WriteEnum(out, type); });
    writer.Vector(environment.typedefs, [](Writer& out, const TypedefSignature& type) { WriteTypedef(out, type); });
    writer.Vector(environment.funcdefs, [](Writer& out, const FuncdefSignature& type) { WriteFuncdef(out, type); });
}

bool ReadEnvironment(Reader& reader, ModuleCompilationEnvironment& environment) {
    std::uint64_t classes = 0;
    if (!reader.Vector(environment.functions, [](Reader& in, FunctionSignature& function) { return ReadFunctionSignature(in, function); }) ||
        !reader.Scalar(classes) || classes > kMaxItems) return false;
    environment.classes.clear(); environment.classes.reserve(static_cast<std::size_t>(classes));
    for (std::uint64_t index = 0; index < classes; ++index) {
        ClassSignature type; if (!ReadClass(reader, type)) return false;
        environment.classes.push_back(std::move(type));
    }
    return reader.Vector(environment.globals, [](Reader& in, GlobalSignature& global) { return ReadGlobal(in, global); }) &&
        reader.Vector(environment.enums, [](Reader& in, EnumSignature& type) { return ReadEnum(in, type); }) &&
        reader.Vector(environment.typedefs, [](Reader& in, TypedefSignature& type) { return ReadTypedef(in, type); }) &&
        reader.Vector(environment.funcdefs, [](Reader& in, FuncdefSignature& type) { return ReadFuncdef(in, type); });
}

void WriteNode(Writer& writer, const AstNode* node) {
    writer.Scalar(node->kind);
    writer.Scalar(node->token.kind);
    writer.String(node->token.lexeme);
    WriteLocation(writer, node->token.location);
    WriteType(writer, node->declaredType);
    WriteType(writer, node->inferredType);
    writer.Scalar<std::uint8_t>(node->isConst ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isAuto ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isGlobal ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isPostfix ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->implicitThis ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isConstructor ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isDestructor ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isDeleted ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->hasExplicitSuper ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->nonVirtualCall ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->returnsReference ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->returnReferenceConst ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->propertyAccessor ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isImported ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isShared ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isExternal ? 1 : 0);
    writer.Scalar<std::uint8_t>(node->isMixinMember ? 1 : 0);
    writer.String(node->sourceModule);
    writer.String(node->operatorMethod);
    writer.Scalar<std::uint8_t>(node->operatorReversed ? 1 : 0);
    writer.String(node->propertyGetter);
    writer.String(node->propertySetter);
    writer.String(node->delegateObjectType);
    writer.Vector(node->captureNames,
        [](Writer& out, const std::string& value) { out.String(value); });
    writer.Scalar(node->memberAccess);
    writer.Scalar(node->parameterMode);
    std::uint64_t children = 0;
    for (const AstNode* child = node->firstChild; child; child = child->nextSibling)
        ++children;
    writer.Scalar(children);
    for (const AstNode* child = node->firstChild; child; child = child->nextSibling)
        WriteNode(writer, child);
}

bool ReadNode(Reader& reader, AstArena& arena, AstNode*& result,
              std::size_t depth, std::uint64_t& nodes) {
    if (depth > 1024 || ++nodes > kMaxItems) return false;
    NodeKind kind{};
    Token token;
    if (!reader.Scalar(kind) || kind > NodeKind::MixinDecl ||
        !reader.Scalar(token.kind) || token.kind > TokenKind::KwMixin ||
        !reader.String(token.lexeme) || !ReadLocation(reader, token.location)) return false;
    AstNode* node = arena.Make(kind, token);
    if (!ReadType(reader, node->declaredType) || !ReadType(reader, node->inferredType) ||
        !ReadBool(reader, node->isConst) || !ReadBool(reader, node->isAuto) ||
        !ReadBool(reader, node->isGlobal) || !ReadBool(reader, node->isPostfix) ||
        !ReadBool(reader, node->implicitThis) || !ReadBool(reader, node->isConstructor) ||
        !ReadBool(reader, node->isDestructor) || !ReadBool(reader, node->isDeleted) ||
        !ReadBool(reader, node->hasExplicitSuper) || !ReadBool(reader, node->nonVirtualCall) ||
        !ReadBool(reader, node->returnsReference) ||
        !ReadBool(reader, node->returnReferenceConst) ||
        !ReadBool(reader, node->propertyAccessor) ||
        !ReadBool(reader, node->isImported) || !ReadBool(reader, node->isShared) ||
        !ReadBool(reader, node->isExternal) ||
        !ReadBool(reader, node->isMixinMember) ||
        !reader.String(node->sourceModule) ||
        !reader.String(node->operatorMethod) ||
        !ReadBool(reader, node->operatorReversed) || !reader.String(node->propertyGetter) ||
        !reader.String(node->propertySetter) || !reader.String(node->delegateObjectType) ||
        !reader.Vector(node->captureNames,
            [](Reader& in, std::string& value) { return in.String(value); }) ||
        !reader.Scalar(node->memberAccess) || node->memberAccess > MemberAccess::Private ||
        !reader.Scalar(node->parameterMode) || node->parameterMode > ParameterMode::InOut)
        return false;
    std::uint64_t children = 0;
    if (!reader.Scalar(children) || children > kMaxItems - nodes) return false;
    for (std::uint64_t index = 0; index < children; ++index) {
        AstNode* child = nullptr;
        if (!ReadNode(reader, arena, child, depth + 1, nodes)) return false;
        node->AppendChild(child);
    }
    result = node;
    return true;
}

void WriteTree(Writer& writer, const std::shared_ptr<const SyntaxTree>& tree) {
    writer.Scalar<std::uint8_t>(tree && tree->root ? 1 : 0);
    if (tree && tree->root) WriteNode(writer, tree->root);
}

bool ReadTree(Reader& reader, std::shared_ptr<const SyntaxTree>& result) {
    bool present = false;
    if (!ReadBool(reader, present)) return false;
    auto tree = std::make_shared<SyntaxTree>();
    if (present) {
        std::uint64_t nodes = 0;
        if (!ReadNode(reader, tree->arena, tree->root, 0, nodes)) return false;
    }
    result = std::move(tree);
    return true;
}

} // namespace

bool WriteBytecodeArchive(std::ostream& output, const BytecodeArchive& archive,
                          std::string& error) {
    Writer payload;
    bool valid = true;
    WriteModule(payload, archive.bytecode, valid);
    WriteEnvironment(payload, archive.environment, valid);
    payload.Vector(archive.removedFunctions,
        [](Writer& out, FunctionId id) { WriteId(out, id); });
    payload.Vector(archive.definitionTrees,
        [](Writer& out, const std::shared_ptr<const SyntaxTree>& tree) {
            WriteTree(out, tree);
        });
    if (!valid) {
        error = "module contains runtime values that cannot be stored in bytecode";
        return false;
    }
    Writer header;
    for (char value : kMagic) header.Scalar<std::uint8_t>(static_cast<std::uint8_t>(value));
    header.Scalar(kVersion);
    header.Scalar<std::uint64_t>(payload.Data().size());
    const auto& prefix = header.Data();
    const auto& body = payload.Data();
    output.write(reinterpret_cast<const char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
    output.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    const std::uint64_t checksum = Checksum(body.data(), body.size());
    Writer trailer; trailer.Scalar(checksum);
    output.write(reinterpret_cast<const char*>(trailer.Data().data()),
                 static_cast<std::streamsize>(trailer.Data().size()));
    if (!output) { error = "failed to write bytecode stream"; return false; }
    return true;
}

bool ReadBytecodeArchive(std::istream& input, BytecodeArchive& archive,
                         std::string& error) {
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) { error = "failed to read bytecode stream"; return false; }
    Reader reader(bytes.data(), bytes.size());
    for (char expected : kMagic) {
        std::uint8_t actual = 0;
        if (!reader.Scalar(actual) || actual != static_cast<std::uint8_t>(expected)) {
            error = "invalid bytecode magic"; return false;
        }
    }
    std::uint32_t version = 0;
    std::uint64_t payloadSize = 0;
    if (!reader.Scalar(version)) { error = "truncated bytecode version"; return false; }
    if (version != kVersion) { error = "unsupported bytecode version"; return false; }
    if (!reader.Scalar(payloadSize) || payloadSize > bytes.size() ||
        bytes.size() != 4 + sizeof(version) + sizeof(payloadSize) + payloadSize + sizeof(std::uint64_t)) {
        error = "invalid bytecode length"; return false;
    }
    const std::size_t payloadOffset = 4 + sizeof(version) + sizeof(payloadSize);
    const std::size_t checksumOffset = payloadOffset + static_cast<std::size_t>(payloadSize);
    Reader checksumReader(bytes.data() + checksumOffset, sizeof(std::uint64_t));
    std::uint64_t storedChecksum = 0;
    if (!checksumReader.Scalar(storedChecksum) ||
        storedChecksum != Checksum(bytes.data() + payloadOffset,
                                   static_cast<std::size_t>(payloadSize))) {
        error = "bytecode checksum mismatch"; return false;
    }
    Reader payload(bytes.data() + payloadOffset, static_cast<std::size_t>(payloadSize));
    BytecodeArchive candidate;
    if (!ReadModule(payload, candidate.bytecode) ||
        !ReadEnvironment(payload, candidate.environment) ||
        !payload.Vector(candidate.removedFunctions,
            [](Reader& in, FunctionId& id) { return ReadId(in, id); }) ||
        !payload.Vector(candidate.definitionTrees,
            [](Reader& in, std::shared_ptr<const SyntaxTree>& tree) {
                return ReadTree(in, tree);
            }) ||
        !payload.Done()) {
        error = payload.Error().empty() ? "invalid bytecode payload" : payload.Error();
        return false;
    }
    archive = std::move(candidate);
    return true;
}

} // namespace mini_as::detail
