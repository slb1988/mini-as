#include "mini_as/engine.hpp"

#include "mini_as/addons/any.hpp"
#include "mini_as/addons/array.hpp"
#include "mini_as/addons/dictionary.hpp"
#include "mini_as/addons/ref.hpp"

#include <cstring>
#include <istream>
#include <iterator>
#include <limits>
#include <ostream>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace mini_as {
namespace {

constexpr char kStateMagic[] = {'M', 'A', 'S', 'S'};
constexpr std::uint32_t kStateVersion = 1;
constexpr std::uint64_t kMaxItems = 1'000'000;
constexpr std::uint64_t kMaxString = 16 * 1024 * 1024;
constexpr std::uint64_t kMaxPayload = 256 * 1024 * 1024;

template <typename T, bool IsEnum = std::is_enum_v<T>>
struct ScalarStorage { using type = T; };

template <typename T>
struct ScalarStorage<T, true> { using type = std::underlying_type_t<T>; };

template <typename T>
using ScalarStorageT = typename ScalarStorage<T>::type;

class Writer {
public:
    template <typename T>
    void Scalar(T value) {
        using Stored = ScalarStorageT<T>;
        using Unsigned = std::make_unsigned_t<Stored>;
        const Unsigned bits = static_cast<Unsigned>(static_cast<Stored>(value));
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

    const std::vector<std::uint8_t>& Data() const { return data_; }

private:
    std::vector<std::uint8_t> data_;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& data)
        : data_(data.data()), size_(data.size()) {}

    template <typename T>
    bool Scalar(T& value) {
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
        std::uint64_t length = 0;
        if (!Scalar(length)) return false;
        if (length > kMaxString || length > Remaining())
            return Fail("invalid string length");
        value.assign(reinterpret_cast<const char*>(data_ + position_),
                     static_cast<std::size_t>(length));
        position_ += static_cast<std::size_t>(length);
        return true;
    }

    bool Count(std::size_t& value) {
        std::uint64_t count = 0;
        if (!Scalar(count)) return false;
        if (count > kMaxItems) return Fail("collection is too large");
        value = static_cast<std::size_t>(count);
        return true;
    }

    bool Done() const { return position_ == size_; }
    bool Fail(std::string error) {
        if (error_.empty()) error_ = std::move(error);
        return false;
    }
    const std::string& Error() const { return error_; }

private:
    std::size_t Remaining() const { return size_ - position_; }
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    std::string error_;
};

std::uint64_t Checksum(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
void WriteStreamScalar(std::ostream& output, T value) {
    Writer writer;
    writer.Scalar(value);
    const auto& bytes = writer.Data();
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

template <typename T>
bool ReadStreamScalar(std::istream& input, T& value) {
    std::vector<std::uint8_t> bytes(sizeof(ScalarStorageT<T>));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input) return false;
    Reader reader(bytes);
    return reader.Scalar(value);
}

void WriteType(Writer& writer, const DataType& type) {
    writer.Scalar(type.kind);
    writer.String(type.objectName);
    writer.Scalar<std::uint8_t>(type.isHandle ? 1 : 0);
}

bool ReadType(Reader& reader, DataType& type) {
    std::uint8_t handle = 0;
    if (!reader.Scalar(type.kind) || !reader.String(type.objectName) ||
        !reader.Scalar(handle)) return false;
    if (type.kind > TypeKind::Invalid || handle > 1)
        return reader.Fail("invalid data type");
    type.isHandle = handle != 0;
    return true;
}

enum class ValueTag : std::uint8_t {
    Void, Bool, Integer, Float, Double, String, Object, Function,
    WeakObject, CapturedCell, ScriptRef, DictionaryValue
};

enum class ObjectKind : std::uint8_t { Script, Array, Dictionary, Any };

class SaveGraph {
public:
    SaveGraph(const ScriptModule& module, const ScriptEngine& engine)
        : module_(module), engine_(engine) {}

    bool Discover(const Value& value) {
        try { return DiscoverValue(value); }
        catch (const std::exception& error) {
            error_ = error.what();
            return false;
        }
    }

    std::uint32_t ObjectId(const RefObject* object) const {
        if (!object) return 0;
        const auto found = objectIds_.find(object);
        return found == objectIds_.end() ? 0 : found->second;
    }

    std::uint32_t CellId(const CapturedCellHandle& cell) const {
        if (!cell) return 0;
        const auto found = cellIds_.find(cell.get());
        return found == cellIds_.end() ? 0 : found->second;
    }

    const std::vector<const RefObject*>& Objects() const { return objects_; }
    const std::vector<CapturedCellHandle>& Cells() const { return cells_; }
    const std::string& Error() const { return error_; }
    const ScriptModule& Module() const { return module_; }
    const ScriptEngine& Engine() const { return engine_; }

private:
    bool AddObject(const ObjectHandle& handle) {
        if (!handle) return true;
        RefObject* object = handle.Get();
        if (objectIds_.count(object)) return true;
        if (objects_.size() >= kMaxItems) {
            error_ = "object graph is too large";
            return false;
        }
        objectIds_.emplace(object,
            static_cast<std::uint32_t>(objects_.size() + 1));
        objects_.push_back(object);
        return DiscoverObject(*object);
    }

    bool AddCell(const CapturedCellHandle& cell) {
        if (!cell || cellIds_.count(cell.get())) return true;
        if (cells_.size() >= kMaxItems) {
            error_ = "captured-cell graph is too large";
            return false;
        }
        cellIds_.emplace(cell.get(),
            static_cast<std::uint32_t>(cells_.size() + 1));
        cells_.push_back(cell);
        return DiscoverValue(cell->value);
    }

    bool DiscoverValue(const Value& value) {
        const auto& raw = value.Raw();
        if (const auto* object = std::get_if<ObjectHandle>(&raw))
            return AddObject(*object);
        if (const auto* function = std::get_if<FunctionHandle>(&raw)) {
            if (!AddObject(function->object)) return false;
            for (const auto& cell : function->captures)
                if (!AddCell(cell)) return false;
            return true;
        }
        if (const auto* cell = std::get_if<CapturedCellHandle>(&raw))
            return AddCell(*cell);
        if (const auto* host = std::get_if<HostValueStorage>(&raw)) {
            if (host->typeName == "ref")
                return AddObject(addons::GetScriptRef(value));
            if (host->typeName == "dictionaryValue")
                return DiscoverValue(value.AsHostValue<addons::DictionaryValue>().value);
            error_ = "registered value type '" + host->typeName +
                "' has no state serialization codec";
            return false;
        }
        if (std::holds_alternative<ReferenceStorage>(raw)) {
            error_ = "runtime references cannot be stored in module state";
            return false;
        }
        return true;
    }

    bool DiscoverObject(const RefObject& object) {
        if (const auto* script = dynamic_cast<const ScriptObject*>(&object)) {
            for (std::size_t index = 0; index < script->FieldCount(); ++index)
                if (!DiscoverValue(script->GetField(index))) return false;
            return true;
        }
        if (const auto* array = dynamic_cast<const addons::ScriptArray*>(&object)) {
            if (!DiscoverValue(array->DefaultElement())) return false;
            for (std::size_t index = 0; index < array->Size(); ++index)
                if (!DiscoverValue(array->Get(index))) return false;
            return true;
        }
        if (const auto* dictionary =
                dynamic_cast<const addons::ScriptDictionary*>(&object)) {
            for (std::size_t index = 0; index < dictionary->Size(); ++index)
                if (!DiscoverValue(dictionary->ValueAt(index))) return false;
            return true;
        }
        if (const auto* any = dynamic_cast<const addons::ScriptAny*>(&object))
            return DiscoverValue(any->StoredValue());
        const TypeInfo* type = object.GetTypeInfo();
        error_ = "reference type '" + (type ? type->name : std::string("<unknown>")) +
            "' has no state serialization codec";
        return false;
    }

    const ScriptModule& module_;
    const ScriptEngine& engine_;
    std::unordered_map<const RefObject*, std::uint32_t> objectIds_;
    std::vector<const RefObject*> objects_;
    std::unordered_map<const CapturedCell*, std::uint32_t> cellIds_;
    std::vector<CapturedCellHandle> cells_;
    std::string error_;
};

bool ResolveFunctionDescriptor(const SaveGraph& graph, const FunctionHandle& handle,
                               std::string& moduleName, std::string& declaration,
                               std::string& objectType) {
    if (!handle.function.IsValid()) return handle.virtualMethod;
    const FunctionMetadata* metadata =
        graph.Engine().GetFunctionMetadataById(handle.function);
    if (metadata) {
        moduleName = metadata->moduleName;
        declaration = metadata->signature.Declaration();
        objectType = metadata->signature.objectType;
        return true;
    }
    const BytecodeFunction* function = graph.Module().Bytecode().FindFunction(handle.function);
    if (!function) return false;
    moduleName = graph.Module().GetName();
    declaration = function->signature.Declaration();
    objectType = function->signature.objectType;
    return true;
}

bool WriteValue(Writer& writer, const SaveGraph& graph, const Value& value,
                std::string& error) {
    const auto& raw = value.Raw();
    if (std::holds_alternative<std::monostate>(raw)) {
        writer.Scalar(ValueTag::Void);
    } else if (const auto* booleanValue = std::get_if<bool>(&raw)) {
        writer.Scalar(ValueTag::Bool);
        writer.Scalar<std::uint8_t>(*booleanValue ? 1 : 0);
    } else if (std::holds_alternative<std::int32_t>(raw) ||
               std::holds_alternative<IntegerStorage>(raw)) {
        writer.Scalar(ValueTag::Integer);
        WriteType(writer, value.Type());
        writer.Scalar(value.UnsignedInteger());
    } else if (const auto* floatValue = std::get_if<float>(&raw)) {
        writer.Scalar(ValueTag::Float);
        writer.Float(*floatValue);
    } else if (const auto* doubleValue = std::get_if<double>(&raw)) {
        writer.Scalar(ValueTag::Double);
        writer.Double(*doubleValue);
    } else if (const auto* stringValue = std::get_if<std::string>(&raw)) {
        if (stringValue->size() > kMaxString) {
            error = "string value is too large";
            return false;
        }
        writer.Scalar(ValueTag::String);
        writer.String(*stringValue);
    } else if (const auto* object = std::get_if<ObjectHandle>(&raw)) {
        writer.Scalar(ValueTag::Object);
        writer.Scalar(graph.ObjectId(object->Get()));
    } else if (const auto* function = std::get_if<FunctionHandle>(&raw)) {
        writer.Scalar(ValueTag::Function);
        writer.String(function->typeName);
        writer.Scalar<std::uint8_t>(static_cast<bool>(*function) ? 1 : 0);
        if (!*function) return true;
        writer.Scalar<std::uint8_t>(function->host ? 1 : 0);
        writer.Scalar<std::uint8_t>(function->virtualMethod ? 1 : 0);
        writer.Scalar(function->virtualSlot);
        writer.Scalar(graph.ObjectId(function->object.Get()));
        std::string moduleName, declaration, objectType;
        if (!ResolveFunctionDescriptor(
                graph, *function, moduleName, declaration, objectType)) {
            error = "function handle target is unavailable";
            return false;
        }
        writer.String(moduleName);
        writer.String(declaration);
        writer.String(objectType);
        std::string dispatchType;
        if (function->dispatchType.IsValid()) {
            const TypeMetadata* metadata =
                graph.Engine().GetTypeMetadataById(function->dispatchType);
            if (!metadata) {
                error = "function handle dispatch type is unavailable";
                return false;
            }
            dispatchType = metadata->name;
        }
        writer.String(dispatchType);
        if (function->captures.size() > kMaxItems) {
            error = "function capture list is too large";
            return false;
        }
        writer.Scalar<std::uint64_t>(function->captures.size());
        for (const auto& capture : function->captures)
            writer.Scalar(graph.CellId(capture));
    } else if (const auto* weak = std::get_if<WeakObjectHandle>(&raw)) {
        writer.Scalar(ValueTag::WeakObject);
        writer.String(weak->TypeName());
        writer.Scalar<std::uint8_t>(weak->IsReadOnly() ? 1 : 0);
        const ObjectHandle target = weak->Lock();
        writer.Scalar(graph.ObjectId(target.Get()));
    } else if (const auto* cell = std::get_if<CapturedCellHandle>(&raw)) {
        writer.Scalar(ValueTag::CapturedCell);
        writer.Scalar(graph.CellId(*cell));
    } else if (const auto* host = std::get_if<HostValueStorage>(&raw)) {
        if (host->typeName == "ref") {
            writer.Scalar(ValueTag::ScriptRef);
            writer.Scalar(graph.ObjectId(addons::GetScriptRef(value).Get()));
        } else if (host->typeName == "dictionaryValue") {
            writer.Scalar(ValueTag::DictionaryValue);
            return WriteValue(writer, graph,
                value.AsHostValue<addons::DictionaryValue>().value, error);
        } else {
            error = "registered value type '" + host->typeName +
                "' has no state serialization codec";
            return false;
        }
    } else {
        error = "runtime references cannot be stored in module state";
        return false;
    }
    return true;
}

ObjectKind KindOf(const RefObject& object) {
    if (dynamic_cast<const ScriptObject*>(&object)) return ObjectKind::Script;
    if (dynamic_cast<const addons::ScriptArray*>(&object)) return ObjectKind::Array;
    if (dynamic_cast<const addons::ScriptDictionary*>(&object)) return ObjectKind::Dictionary;
    return ObjectKind::Any;
}

bool WriteObjectShell(Writer& writer, const RefObject& object) {
    const TypeInfo* type = object.GetTypeInfo();
    if (!type) return false;
    const ObjectKind kind = KindOf(object);
    writer.Scalar(kind);
    writer.String(type->name);
    if (kind == ObjectKind::Script) {
        writer.Scalar<std::uint64_t>(type->fields.size());
        for (const auto& field : type->fields) {
            writer.String(field.first);
            WriteType(writer, field.second);
        }
    } else if (kind == ObjectKind::Array) {
        WriteType(writer,
            static_cast<const addons::ScriptArray&>(object).ElementType());
    }
    return true;
}

bool WriteObjectPayload(Writer& writer, const SaveGraph& graph,
                        const RefObject& object, std::string& error) {
    if (const auto* script = dynamic_cast<const ScriptObject*>(&object)) {
        if (script->FieldCount() > kMaxItems) {
            error = "script object has too many fields";
            return false;
        }
        writer.Scalar<std::uint64_t>(script->FieldCount());
        for (std::size_t index = 0; index < script->FieldCount(); ++index)
            if (!WriteValue(writer, graph, script->GetField(index), error)) return false;
        return true;
    }
    if (const auto* array = dynamic_cast<const addons::ScriptArray*>(&object)) {
        if (array->Size() > kMaxItems) {
            error = "array is too large";
            return false;
        }
        if (!WriteValue(writer, graph, array->DefaultElement(), error)) return false;
        writer.Scalar<std::uint64_t>(array->Size());
        for (std::size_t index = 0; index < array->Size(); ++index)
            if (!WriteValue(writer, graph, array->Get(index), error)) return false;
        return true;
    }
    if (const auto* dictionary =
            dynamic_cast<const addons::ScriptDictionary*>(&object)) {
        if (dictionary->Size() > kMaxItems) {
            error = "dictionary is too large";
            return false;
        }
        writer.Scalar<std::uint64_t>(dictionary->Size());
        for (std::size_t index = 0; index < dictionary->Size(); ++index) {
            if (dictionary->KeyAt(index).size() > kMaxString) {
                error = "dictionary key is too large";
                return false;
            }
            writer.String(dictionary->KeyAt(index));
            if (!WriteValue(writer, graph, dictionary->ValueAt(index), error)) return false;
        }
        return true;
    }
    const auto* any = dynamic_cast<const addons::ScriptAny*>(&object);
    return any && WriteValue(writer, graph, any->StoredValue(), error);
}

struct ObjectShell {
    ObjectKind kind = ObjectKind::Script;
    std::string typeName;
    std::vector<std::pair<std::string, DataType>> fields;
    DataType elementType = DataType::Invalid();
};

bool ValueMatchesType(const Value& value, const DataType& expected) {
    if (expected.kind == TypeKind::Object) {
        if (const auto* object = std::get_if<ObjectHandle>(&value.Raw())) {
            if (!*object) return expected.isHandle;
            const TypeInfo* type = object->Get()->GetTypeInfo();
            return type && (type->IsA(expected.objectName) || type->name == expected.objectName);
        }
        if (const auto* host = std::get_if<HostValueStorage>(&value.Raw()))
            return host->typeName == expected.objectName;
        return false;
    }
    if (expected.kind == TypeKind::Function) {
        const auto* function = std::get_if<FunctionHandle>(&value.Raw());
        return function && function->typeName == expected.objectName;
    }
    if (expected.kind == TypeKind::WeakRef || expected.kind == TypeKind::ConstWeakRef) {
        const auto* weak = std::get_if<WeakObjectHandle>(&value.Raw());
        return weak && weak->TypeName() == expected.objectName &&
            weak->IsReadOnly() == (expected.kind == TypeKind::ConstWeakRef);
    }
    return value.Type() == expected;
}

const FunctionMetadata* FindFunctionMetadata(const ScriptEngine& engine,
                                             std::string_view moduleName,
                                             std::string_view declaration,
                                             std::string_view objectType,
                                             bool host) {
    for (std::size_t index = 0; index < engine.GetFunctionMetadataCount(); ++index) {
        const FunctionMetadata* metadata = engine.GetFunctionMetadataByIndex(index);
        if (metadata && metadata->moduleName == moduleName &&
            metadata->signature.Declaration() == declaration &&
            metadata->signature.objectType == objectType &&
            metadata->signature.host == host) return metadata;
    }
    return nullptr;
}

bool ReadValue(Reader& reader, ScriptEngine& engine, const ScriptModule& module,
               const std::vector<ObjectHandle>& objects,
               const std::vector<CapturedCellHandle>& cells, Value& value) {
    ValueTag tag = ValueTag::Void;
    if (!reader.Scalar(tag) || tag > ValueTag::DictionaryValue)
        return reader.Fail("invalid value tag");
    switch (tag) {
    case ValueTag::Void: value = Value{}; return true;
    case ValueTag::Bool: {
        std::uint8_t stored = 0;
        if (!reader.Scalar(stored) || stored > 1) return reader.Fail("invalid boolean");
        value = Value(stored != 0); return true;
    }
    case ValueTag::Integer: {
        DataType type;
        std::uint64_t bits = 0;
        if (!ReadType(reader, type) || !reader.Scalar(bits) || !type.IsInteger())
            return reader.Fail("invalid integer value");
        value = Value::Integer(type, bits); return true;
    }
    case ValueTag::Float: {
        float stored = 0;
        if (!reader.Float(stored)) return false;
        value = Value(stored); return true;
    }
    case ValueTag::Double: {
        double stored = 0;
        if (!reader.Double(stored)) return false;
        value = Value(stored); return true;
    }
    case ValueTag::String: {
        std::string stored;
        if (!reader.String(stored)) return false;
        value = Value(std::move(stored)); return true;
    }
    case ValueTag::Object: {
        std::uint32_t id = 0;
        if (!reader.Scalar(id) || id > objects.size())
            return reader.Fail("invalid object reference");
        value = Value(id ? objects[id - 1] : ObjectHandle{}); return true;
    }
    case ValueTag::CapturedCell: {
        std::uint32_t id = 0;
        if (!reader.Scalar(id) || id > cells.size())
            return reader.Fail("invalid captured-cell reference");
        value = Value(id ? cells[id - 1] : CapturedCellHandle{}); return true;
    }
    case ValueTag::WeakObject: {
        std::string typeName;
        std::uint8_t readOnly = 0;
        std::uint32_t id = 0;
        if (!reader.String(typeName) || !reader.Scalar(readOnly) || readOnly > 1 ||
            !reader.Scalar(id) || id > objects.size())
            return reader.Fail("invalid weak object reference");
        value = Value(id ? WeakObjectHandle(objects[id - 1], typeName, readOnly != 0)
                         : WeakObjectHandle(typeName, readOnly != 0));
        return true;
    }
    case ValueTag::ScriptRef: {
        std::uint32_t id = 0;
        if (!reader.Scalar(id) || id > objects.size())
            return reader.Fail("invalid ref object reference");
        value = addons::MakeScriptRef(id ? objects[id - 1] : ObjectHandle{});
        return true;
    }
    case ValueTag::DictionaryValue: {
        Value stored;
        if (!ReadValue(reader, engine, module, objects, cells, stored)) return false;
        value = Value::HostValue("dictionaryValue",
                                 addons::DictionaryValue{std::move(stored)});
        return true;
    }
    case ValueTag::Function: {
        std::string typeName;
        std::uint8_t present = 0;
        if (!reader.String(typeName) || !reader.Scalar(present) || present > 1)
            return reader.Fail("invalid function handle");
        if (!present) {
            value = Value(FunctionHandle{{}, {}, std::move(typeName), false});
            return true;
        }
        std::uint8_t host = 0, virtualMethod = 0;
        std::uint32_t virtualSlot = 0, objectId = 0;
        std::string moduleName, declaration, objectType, dispatchType;
        if (!reader.Scalar(host) || host > 1 || !reader.Scalar(virtualMethod) ||
            virtualMethod > 1 || !reader.Scalar(virtualSlot) ||
            !reader.Scalar(objectId) || objectId > objects.size() ||
            !reader.String(moduleName) || !reader.String(declaration) ||
            !reader.String(objectType) || !reader.String(dispatchType))
            return reader.Fail("invalid function handle descriptor");
        FunctionId function;
        if (!declaration.empty()) {
            const FunctionMetadata* metadata = FindFunctionMetadata(
                engine, moduleName, declaration, objectType, host != 0);
            if (!metadata) return reader.Fail("function handle target does not match module");
            function = metadata->id;
        }
        TypeId signature;
        for (const auto& funcdef : module.Bytecode().funcdefs)
            if (funcdef.name == typeName) signature = funcdef.id;
        if (!signature.IsValid())
            return reader.Fail("function handle type does not match module");
        TypeId dispatch;
        if (!dispatchType.empty()) {
            const TypeInfo* type = engine.GetTypeInfo(dispatchType);
            if (!type) return reader.Fail("function dispatch type does not match engine");
            dispatch = type->id;
        }
        std::size_t captureCount = 0;
        if (!reader.Count(captureCount)) return false;
        std::vector<CapturedCellHandle> captures;
        captures.reserve(captureCount);
        for (std::size_t index = 0; index < captureCount; ++index) {
            std::uint32_t id = 0;
            if (!reader.Scalar(id) || id == 0 || id > cells.size())
                return reader.Fail("invalid function capture reference");
            captures.push_back(cells[id - 1]);
        }
        value = Value(FunctionHandle{function, signature, std::move(typeName), host != 0,
            objectId ? objects[objectId - 1] : ObjectHandle{}, dispatch, virtualSlot,
            virtualMethod != 0, std::move(captures)});
        return true;
    }
    }
    return reader.Fail("invalid value tag");
}

bool ReadPayload(std::istream& input, std::vector<std::uint8_t>& payload,
                 std::string& error) {
    char magic[sizeof(kStateMagic)]{};
    input.read(magic, sizeof(magic));
    if (!input || std::memcmp(magic, kStateMagic, sizeof(magic)) != 0) {
        error = "invalid state archive magic";
        return false;
    }
    std::uint32_t version = 0;
    std::uint64_t size = 0, expectedChecksum = 0;
    if (!ReadStreamScalar(input, version) || !ReadStreamScalar(input, size) ||
        !ReadStreamScalar(input, expectedChecksum)) {
        error = "truncated state archive header";
        return false;
    }
    if (version != kStateVersion) {
        error = "unsupported state archive version";
        return false;
    }
    if (size > kMaxPayload) {
        error = "state archive payload is too large";
        return false;
    }
    payload.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(payload.data()),
               static_cast<std::streamsize>(payload.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        error = "truncated state archive payload";
        if (input && input.peek() != std::char_traits<char>::eof())
            error = "trailing data after state archive";
        return false;
    }
    if (Checksum(payload.data(), payload.size()) != expectedChecksum) {
        error = "state archive checksum mismatch";
        return false;
    }
    return true;
}

} // namespace

bool ScriptModule::SaveState(std::ostream& output) const {
    if (!image_ || !image_->state) {
        engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                   "state save failed: module is not built"});
        return false;
    }
    SaveGraph graph(*this, engine_);
    std::vector<std::size_t> globalIndices;
    for (std::size_t index = 0; index < image_->bytecode.globals.size(); ++index) {
        if (image_->bytecode.globals[index].host) continue;
        globalIndices.push_back(index);
        if (index >= image_->state->globals.size() ||
            !graph.Discover(image_->state->globals[index])) {
            const std::string reason = graph.Error().empty()
                ? "global slot is unavailable" : graph.Error();
            engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                       "state save failed: " + reason});
            return false;
        }
    }

    Writer writer;
    writer.String(name_);
    writer.Scalar<std::uint64_t>(globalIndices.size());
    for (const std::size_t index : globalIndices) {
        const auto& signature = image_->bytecode.globals[index].signature;
        writer.String(signature.name);
        WriteType(writer, signature.type);
    }
    writer.Scalar<std::uint64_t>(graph.Objects().size());
    for (const RefObject* object : graph.Objects()) {
        if (!object || !WriteObjectShell(writer, *object)) {
            engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                       "state save failed: object type is unavailable"});
            return false;
        }
    }
    writer.Scalar<std::uint64_t>(graph.Cells().size());
    std::string error;
    for (const std::size_t index : globalIndices)
        if (!WriteValue(writer, graph, image_->state->globals[index], error)) {
            engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                       "state save failed: " + error});
            return false;
        }
    for (const RefObject* object : graph.Objects())
        if (!WriteObjectPayload(writer, graph, *object, error)) {
            engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                       "state save failed: " + error});
            return false;
        }
    for (const auto& cell : graph.Cells())
        if (!WriteValue(writer, graph, cell->value, error)) {
            engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                       "state save failed: " + error});
            return false;
        }

    if (writer.Data().size() > kMaxPayload) {
        engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                   "state save failed: state archive payload is too large"});
        return false;
    }

    output.write(kStateMagic, sizeof(kStateMagic));
    WriteStreamScalar(output, kStateVersion);
    WriteStreamScalar<std::uint64_t>(output, writer.Data().size());
    WriteStreamScalar(output, Checksum(writer.Data().data(), writer.Data().size()));
    output.write(reinterpret_cast<const char*>(writer.Data().data()),
                 static_cast<std::streamsize>(writer.Data().size()));
    if (output) return true;
    engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                               "state save failed: output stream rejected archive"});
    return false;
}

bool ScriptModule::LoadState(std::istream& input) {
    if (!image_ || !image_->state) {
        engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                   "state load failed: module is not built"});
        return false;
    }
    std::vector<std::uint8_t> payload;
    std::string error;
    if (!ReadPayload(input, payload, error)) {
        engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                   "state load failed: " + error});
        return false;
    }
    Reader reader(payload);
    std::string archivedModule;
    if (!reader.String(archivedModule) || archivedModule != name_) {
        engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                   "state load failed: module name does not match"});
        return false;
    }

    std::vector<std::size_t> globalIndices;
    for (std::size_t index = 0; index < image_->bytecode.globals.size(); ++index)
        if (!image_->bytecode.globals[index].host) globalIndices.push_back(index);
    std::size_t globalCount = 0;
    if (!reader.Count(globalCount) || globalCount != globalIndices.size()) {
        engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                   "state load failed: global schema does not match"});
        return false;
    }
    for (const std::size_t index : globalIndices) {
        std::string name;
        DataType type;
        const auto& current = image_->bytecode.globals[index].signature;
        if (!reader.String(name) || !ReadType(reader, type) ||
            name != current.name || type != current.type) {
            engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                       "state load failed: global schema does not match"});
            return false;
        }
    }

    std::size_t objectCount = 0;
    if (!reader.Count(objectCount)) {
        engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                   "state load failed: " + reader.Error()});
        return false;
    }
    std::vector<ObjectShell> shells(objectCount);
    for (auto& shell : shells) {
        if (!reader.Scalar(shell.kind) || shell.kind > ObjectKind::Any ||
            !reader.String(shell.typeName)) {
            error = reader.Error().empty() ? "invalid object descriptor" : reader.Error();
            engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                       "state load failed: " + error});
            return false;
        }
        if (shell.kind == ObjectKind::Script) {
            std::size_t fieldCount = 0;
            if (!reader.Count(fieldCount)) {
                engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                           "state load failed: " + reader.Error()});
                return false;
            }
            shell.fields.resize(fieldCount);
            for (auto& field : shell.fields)
                if (!reader.String(field.first) || !ReadType(reader, field.second)) {
                    engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                               "state load failed: " + reader.Error()});
                    return false;
                }
        } else if (shell.kind == ObjectKind::Array &&
                   !ReadType(reader, shell.elementType)) {
            engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                       "state load failed: " + reader.Error()});
            return false;
        }
    }
    std::size_t cellCount = 0;
    if (!reader.Count(cellCount)) {
        engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                   "state load failed: " + reader.Error()});
        return false;
    }

    std::vector<ObjectHandle> objects;
    objects.reserve(objectCount);
    for (const auto& shell : shells) {
        const TypeInfo* type = engine_.GetTypeInfo(shell.typeName);
        if (!type) { error = "object type '" + shell.typeName + "' is unavailable"; break; }
        if (shell.kind == ObjectKind::Script) {
            if (!type->script || type->fields != shell.fields) {
                error = "script object schema for '" + shell.typeName + "' does not match";
                break;
            }
            objects.emplace_back(new ScriptObject(type));
        } else if (shell.kind == ObjectKind::Array) {
            if (type->templateBase != "array" || type->templateSubTypes.size() != 1 ||
                type->templateSubTypes[0] != shell.elementType) {
                error = "array object schema for '" + shell.typeName + "' does not match";
                break;
            }
            objects.emplace_back(new addons::ScriptArray(
                type, shell.elementType, Value{}, 0));
        } else if (shell.kind == ObjectKind::Dictionary) {
            if (shell.typeName != "dictionary") {
                error = "dictionary object schema does not match";
                break;
            }
            objects.emplace_back(new addons::ScriptDictionary(type));
        } else {
            if (shell.typeName != "any") {
                error = "any object schema does not match";
                break;
            }
            objects.emplace_back(new addons::ScriptAny(type));
        }
    }
    std::vector<CapturedCellHandle> cells(cellCount);
    for (auto& cell : cells) cell = std::make_shared<CapturedCell>();
    std::vector<Value> candidate = image_->state->globals;
    const auto cleanup = [&] {
        for (auto& value : candidate) value.ClearReferences();
        for (auto& cell : cells) if (cell) cell->value.ClearReferences();
        for (auto& object : objects) if (object) object.Get()->ClearReferences();
        candidate.clear();
        cells.clear();
        objects.clear();
    };
    const auto fail = [&](std::string reason) {
        cleanup();
        engine_.ForwardDiagnostic({{"state"}, Severity::Error,
                                   "state load failed: " + std::move(reason)});
        return false;
    };
    if (!error.empty()) return fail(error);

    for (const std::size_t index : globalIndices) {
        Value value;
        const DataType& expected = image_->bytecode.globals[index].signature.type;
        if (!ReadValue(reader, engine_, *this, objects, cells, value) ||
            !ValueMatchesType(value, expected))
            return fail(reader.Error().empty()
                ? "global value type does not match" : reader.Error());
        candidate[index] = std::move(value);
    }
    for (std::size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        RefObject* object = objects[objectIndex].Get();
        const ObjectShell& shell = shells[objectIndex];
        if (shell.kind == ObjectKind::Script) {
            auto* script = static_cast<ScriptObject*>(object);
            std::size_t fieldCount = 0;
            if (!reader.Count(fieldCount) || fieldCount != shell.fields.size())
                return fail("script object payload does not match schema");
            for (std::size_t field = 0; field < fieldCount; ++field) {
                Value value;
                if (!ReadValue(reader, engine_, *this, objects, cells, value) ||
                    !ValueMatchesType(value, shell.fields[field].second))
                    return fail(reader.Error().empty()
                        ? "script field value type does not match" : reader.Error());
                script->SetField(field, std::move(value));
            }
        } else if (shell.kind == ObjectKind::Array) {
            auto* array = static_cast<addons::ScriptArray*>(object);
            Value defaultElement;
            if (!ReadValue(reader, engine_, *this, objects, cells, defaultElement) ||
                !ValueMatchesType(defaultElement, shell.elementType))
                return fail(reader.Error().empty()
                    ? "array default value type does not match" : reader.Error());
            array->SetDefaultElement(std::move(defaultElement));
            std::size_t size = 0;
            if (!reader.Count(size)) return fail(reader.Error());
            for (std::size_t index = 0; index < size; ++index) {
                Value value;
                if (!ReadValue(reader, engine_, *this, objects, cells, value) ||
                    !ValueMatchesType(value, shell.elementType))
                    return fail(reader.Error().empty()
                        ? "array element type does not match" : reader.Error());
                array->InsertLast(std::move(value));
            }
        } else if (shell.kind == ObjectKind::Dictionary) {
            auto* dictionary = static_cast<addons::ScriptDictionary*>(object);
            std::size_t size = 0;
            if (!reader.Count(size)) return fail(reader.Error());
            for (std::size_t index = 0; index < size; ++index) {
                std::string key;
                Value value;
                if (!reader.String(key) ||
                    !ReadValue(reader, engine_, *this, objects, cells, value))
                    return fail(reader.Error());
                dictionary->Set(std::move(key), std::move(value));
            }
        } else {
            Value value;
            if (!ReadValue(reader, engine_, *this, objects, cells, value))
                return fail(reader.Error());
            static_cast<addons::ScriptAny*>(object)->Store(std::move(value));
        }
    }
    for (auto& cell : cells)
        if (!ReadValue(reader, engine_, *this, objects, cells, cell->value))
            return fail(reader.Error());
    if (!reader.Done()) return fail("trailing data in state payload");

    for (const auto& object : objects) {
        auto* script = dynamic_cast<ScriptObject*>(object.Get());
        if (!script) continue;
        ScriptFinalizerBinding finalizer;
        finalizer.functions = image_->bytecode.FindDestructors(script->GetTypeInfo()->id);
        finalizer.module = image_->finalizerBytecode;
        finalizer.state = image_->state;
        if (!script->BindFinalizer(&engine_, std::move(finalizer)))
            return fail("script object finalizer could not be restored");
    }

    image_->state->globals.swap(candidate);
    candidate.clear();
    cells.clear();
    objects.clear();
    engine_.DrainFinalizers();
    return true;
}

} // namespace mini_as
