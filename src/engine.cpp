#include "mini_as/engine.hpp"

#include <utility>
#include <algorithm>

namespace mini_as {
namespace {

Value DefaultGlobalValue(const DataType& type) {
    if (type == DataType::Bool()) return Value(false);
    if (type.IsInteger()) return Value::Integer(type, 0);
    if (type == DataType::Float()) return Value(0.0f);
    if (type == DataType::Double()) return Value(0.0);
    if (type == DataType::String()) return Value(std::string{});
    if (type.kind == TypeKind::Object) return Value(ObjectHandle{});
    if (type.kind == TypeKind::Function) return Value(FunctionHandle{{}, {}, type.objectName, false});
    return Value{};
}

} // namespace

std::string_view Version() { return "0.1.0-learning"; }

ScriptModule::ScriptModule(ScriptEngine& engine, std::string name)
    : engine_(engine), name_(std::move(name)), image_(std::make_shared<ModuleImage>()) {}

const std::string& ScriptModule::GetName() const { return name_; }

void ScriptModule::AddScriptSection(std::string name, std::string source) {
    sections_.push_back({std::move(name), std::move(source)});
}

bool ScriptModule::Build() {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { engine_.ForwardDiagnostic(diagnostic); });
    std::vector<Token> tokens;
    for (const auto& section : sections_) {
        Tokenizer tokenizer(section.name, section.source, diagnostics);
        auto sectionTokens = tokenizer.ScanAll();
        if (!sectionTokens.empty()) sectionTokens.pop_back();
        tokens.insert(tokens.end(), std::make_move_iterator(sectionTokens.begin()),
                      std::make_move_iterator(sectionTokens.end()));
    }
    SourceLocation endLocation{sections_.empty() ? name_ : sections_.back().name};
    tokens.push_back({TokenKind::End, {}, std::move(endLocation)});
    Parser parser(std::move(tokens), diagnostics);
    auto tree = parser.Parse();
    TypeChecker checker(diagnostics);
    for (const auto& signature : engine_.HostSignatures()) checker.RegisterFunction(signature);
    const bool typed = !diagnostics.HasErrors() && checker.Check(tree.root);
    if (!typed) return false;
    auto functions = checker.Functions();
    for (auto& function : functions) {
        if (!function.id.IsValid()) {
            function.id = engine_.GetOrCreateFunctionId(name_ + "\n" + function.Declaration());
        }
    }
    auto classes = checker.Classes();
    for (auto& type : classes) {
        type.id = engine_.GetOrCreateTypeId(type.name);
        for (auto& method : type.methods) {
            method.objectType = type.name;
            method.method = true;
            method.id = engine_.GetOrCreateFunctionId(
                name_ + "\n" + type.name + "::" + method.Declaration());
        }
    }
    auto globals = checker.Globals();
    for (auto& global : globals)
        global.id = engine_.GetOrCreateGlobalId(name_ + "\n" + global.name);
    BytecodeCompiler compiler(diagnostics);
    auto enums = checker.Enums();
    for (auto& type : enums) type.id = engine_.GetOrCreateTypeId(type.name);
    auto typedefs = checker.Typedefs();
    for (auto& type : typedefs) type.id = engine_.GetOrCreateTypeId(type.name);
    auto funcdefs = checker.Funcdefs();
    for (auto& type : funcdefs) type.id = engine_.GetOrCreateTypeId(type.name);
    BytecodeModule candidate = compiler.Compile(tree.root, functions, classes, globals, enums, funcdefs);
    if (diagnostics.HasErrors()) return false;
    std::vector<const TypeInfo*> scriptTypes;
    for (const auto& type : classes) {
        const TypeInfo* linked = engine_.RegisterScriptType(type);
        scriptTypes.push_back(linked);
    }
    for (const auto& type : classes) engine_.LinkScriptType(type);
    for (const auto& host : engine_.hostFunctions_)
        candidate.hostFunctions.push_back({host.signature.id, &host});
    for (const auto* type : scriptTypes) candidate.objectTypes.push_back({type->id, type});
    auto state = std::make_shared<ModuleState>();
    state->globals.reserve(candidate.globals.size());
    for (const auto& global : candidate.globals)
        state->globals.push_back(DefaultGlobalValue(global.signature.type));
    VirtualMachine initializer;
    auto finalizerModule = std::make_shared<BytecodeModule>(candidate);
    initializer.SetFinalizerContext(&engine_, finalizerModule, state,
                                    [this] { engine_.DrainFinalizers(); });
    const auto initialized = initializer.Execute(candidate.globalInitializer, {}, &candidate, state.get());
    engine_.DrainFinalizers();
    if (initialized.state != ExecutionState::Finished) {
        diagnostics.Report(initialized.location, Severity::Error,
                           "global initialization failed: " + initialized.exception);
        return false;
    }
    auto nextImage = std::make_shared<ModuleImage>();
    nextImage->bytecode = std::move(candidate);
    nextImage->finalizerBytecode = std::move(finalizerModule);
    nextImage->state = std::move(state);
    engine_.RegisterModuleImage(nextImage);
    image_ = std::move(nextImage);
    sections_.clear();
    return true;
}

const BytecodeFunction* ScriptModule::GetFunctionByDecl(std::string_view declaration) const {
    for (const auto& function : image_->bytecode.functions) {
        if (function.signature.Declaration() == declaration) return &function;
    }
    return nullptr;
}

const BytecodeFunction* ScriptModule::GetFunctionByName(std::string_view name) const {
    for (const auto& function : image_->bytecode.functions) if (function.signature.name == name) return &function;
    return nullptr;
}

const BytecodeModule& ScriptModule::Bytecode() const { return image_->bytecode; }

ScriptContext::ScriptContext(ScriptEngine& engine) : engine_(engine) {
    vm_.SetLineCallback([this](const SourceLocation& location) {
        if (lineCallback_) lineCallback_(*this, location);
    });
}

bool ScriptContext::Prepare(const BytecodeFunction* function) {
    image_.reset();
    function_ = nullptr;
    if (!function) {
        result_ = {};
        result_.state = ExecutionState::Exception;
        result_.exception = "cannot prepare a null function";
        return false;
    }
    image_ = engine_.FindModuleImage(function);
    if (!image_) {
        result_ = {};
        result_.state = ExecutionState::Exception;
        result_.exception = "function does not belong to a live module image";
        return false;
    }
    function_ = function;
    arguments_.assign(function_->signature.parameters.size(), Value{});
    result_ = {};
    result_.state = ExecutionState::Prepared;
    return true;
}

bool ScriptContext::SetArgInt(std::size_t index, std::int32_t value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgFloat(std::size_t index, float value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgDouble(std::size_t index, double value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgBool(std::size_t index, bool value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgString(std::size_t index, std::string value) { return SetArgument(index, Value(std::move(value))); }
bool ScriptContext::SetArgObject(std::size_t index, ObjectHandle value) { return SetArgument(index, Value(std::move(value))); }

ExecutionState ScriptContext::Execute() {
    if (!function_) {
        result_ = {};
        result_.state = ExecutionState::Exception;
        result_.exception = "context has no prepared function";
        return result_.state;
    }
    if (result_.state == ExecutionState::Prepared) {
        vm_.SetFinalizerContext(&engine_, image_->finalizerBytecode, image_->state,
                                [this] { engine_.DrainFinalizers(); });
        if (!vm_.Prepare(*function_, arguments_, &image_->bytecode, image_->state.get())) {
            result_ = vm_.Continue();
            engine_.DrainFinalizers();
            return result_.state;
        }
    }
    result_ = vm_.Continue();
    engine_.DrainFinalizers();
    return result_.state;
}

void ScriptContext::Suspend() { vm_.RequestSuspend(); }
void ScriptContext::Abort() {
    vm_.Abort();
    engine_.DrainFinalizers();
    result_.state = ExecutionState::Aborted;
}
void ScriptContext::SetLineCallback(LineCallback callback) { lineCallback_ = std::move(callback); }
ExecutionState ScriptContext::GetState() const { return result_.state; }
const Value& ScriptContext::GetReturnValue() const { return result_.returnValue; }
std::int32_t ScriptContext::GetReturnInt() const { return result_.returnValue.As<std::int32_t>(); }
float ScriptContext::GetReturnFloat() const { return result_.returnValue.As<float>(); }
double ScriptContext::GetReturnDouble() const { return result_.returnValue.As<double>(); }
const std::string& ScriptContext::GetExceptionString() const { return result_.exception; }
const SourceLocation& ScriptContext::GetExceptionLocation() const { return result_.location; }
const std::vector<StackFrameInfo>& ScriptContext::GetCallStack() const { return result_.callStack; }

bool ScriptContext::SetArgument(std::size_t index, Value value) {
    if (!function_ || index >= arguments_.size() || result_.state != ExecutionState::Prepared) return false;
    const DataType expected = function_->signature.parameters[index];
    if (value.Type().IsInteger() && expected.IsInteger()) value = ConvertInteger(value, expected);
    else if (value.Type().IsSignedInteger() && expected == DataType::Float())
        value = Value(static_cast<float>(value.SignedInteger()));
    else if (value.Type().IsUnsignedInteger() && expected == DataType::Float())
        value = Value(static_cast<float>(value.UnsignedInteger()));
    else if (value.Type().IsSignedInteger() && expected == DataType::Double())
        value = Value(static_cast<double>(value.SignedInteger()));
    else if (value.Type().IsUnsignedInteger() && expected == DataType::Double())
        value = Value(static_cast<double>(value.UnsignedInteger()));
    else if (value.Type() == DataType::Float() && expected == DataType::Double())
        value = Value(static_cast<double>(value.As<float>()));
    else if (value.Type() == DataType::Double() && expected == DataType::Float())
        value = Value(static_cast<float>(value.As<double>()));
    else if (value.Type() != expected) {
        if (value.Type().kind == TypeKind::Object && value.Type().objectName == "<null>" && expected.isHandle) {
            arguments_[index] = std::move(value);
            return true;
        }
        if (value.Type().kind != TypeKind::Object || expected.kind != TypeKind::Object ||
            !value.As<ObjectHandle>()) return false;
        const auto* scriptObject = dynamic_cast<ScriptObject*>(value.As<ObjectHandle>().Get());
        if (!scriptObject || (!scriptObject->Implements(expected.objectName) &&
                              !scriptObject->IsA(expected.objectName))) return false;
    }
    arguments_[index] = std::move(value);
    return true;
}

void ScriptEngine::SetMessageCallback(MessageCallback callback) { messageCallback_ = std::move(callback); }

ScriptEngine::~ScriptEngine() {
    for (auto& entry : modules_) {
        if (!entry.second->image_ || !entry.second->image_->state) continue;
        for (auto& global : entry.second->image_->state->globals) global = Value{};
    }
    DrainFinalizers();
    garbageCollector_.Collect();
    DrainFinalizers();
    modules_.clear();
    DrainFinalizers();
}

bool ScriptEngine::RegisterGlobalFunction(std::string declaration, GenericFunction callback) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    auto signature = ParseFunctionDeclaration(declaration, diagnostics);
    if (!signature || !callback) return false;
    if (signature->returnsReference) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "host return references require registered property storage");
        return false;
    }
    for (const auto& existing : hostFunctions_) {
        if (existing.signature.name == signature->name &&
            existing.signature.parameters == signature->parameters) {
            diagnostics.Report({"registration"}, Severity::Error,
                               "duplicate global function '" + signature->Declaration() + "'");
            return false;
        }
    }
    signature->id = GetOrCreateFunctionId("$host\n" + signature->Declaration());
    hostFunctions_.push_back({std::move(*signature), std::move(callback)});
    return true;
}

const TypeInfo* ScriptEngine::RegisterObjectType(std::string name) {
    if (name.empty() || objectTypes_.find(name) != objectTypes_.end()) return nullptr;
    auto type = std::make_unique<TypeInfo>();
    type->name = name;
    type->id = GetOrCreateTypeId(name);
    const TypeInfo* result = type.get();
    objectTypes_.emplace(std::move(name), std::move(type));
    return result;
}

const TypeInfo* ScriptEngine::GetTypeInfo(std::string_view name) const {
    const auto found = objectTypes_.find(std::string(name));
    return found == objectTypes_.end() ? nullptr : found->second.get();
}

std::size_t ScriptEngine::CollectGarbage() {
    const std::size_t collected = garbageCollector_.Collect();
    DrainFinalizers();
    return collected;
}
std::size_t ScriptEngine::GetTrackedObjectCount() const { return garbageCollector_.TrackedCount(); }

const TypeInfo* ScriptEngine::RegisterScriptType(const ClassSignature& signature) {
    TypeInfo* type = nullptr;
    const auto found = objectTypes_.find(signature.name);
    if (found == objectTypes_.end()) {
        auto created = std::make_unique<TypeInfo>();
        created->name = signature.name;
        created->id = signature.id.IsValid() ? signature.id : GetOrCreateTypeId(signature.name);
        type = created.get();
        objectTypes_.emplace(signature.name, std::move(created));
    } else type = found->second.get();
    type->script = !signature.interfaceType;
    type->baseClass = signature.baseClass;
    type->baseType = nullptr;
    type->collector = signature.interfaceType ? nullptr : &garbageCollector_;
    type->fields.clear();
    type->fields.reserve(signature.fields.size());
    for (const auto& field : signature.fields)
        type->fields.emplace_back(field.name, field.type);
    type->interfaces = signature.interfaces;
    type->interfaceMethodTable.clear();
    for (const auto& interfaceName : signature.interfaces) {
        const auto interfaceFound = std::find_if(
            objectTypes_.begin(), objectTypes_.end(),
            [&](const auto& entry) { return entry.first == interfaceName; });
        (void)interfaceFound;
        for (const auto& method : signature.methods) {
            type->interfaceMethodTable[interfaceName + "::" + method.Declaration()] =
                signature.name + "::" + method.Declaration();
        }
    }
    return type;
}

void ScriptEngine::LinkScriptType(const ClassSignature& signature) {
    const auto found = objectTypes_.find(signature.name);
    if (found == objectTypes_.end()) return;
    TypeInfo* type = found->second.get();
    if (signature.baseClass.empty()) { type->baseType = nullptr; return; }
    const auto base = objectTypes_.find(signature.baseClass);
    type->baseType = base == objectTypes_.end() ? nullptr : base->second.get();
}

ScriptModule* ScriptEngine::GetModule(std::string name, ModulePolicy policy) {
    const auto found = modules_.find(name);
    if (policy == ModulePolicy::AlwaysCreate) {
        auto module = std::make_unique<ScriptModule>(*this, name);
        ScriptModule* result = module.get();
        modules_[std::move(name)] = std::move(module);
        return result;
    }
    if (found != modules_.end()) return found->second.get();
    if (policy == ModulePolicy::OnlyIfExists) return nullptr;
    auto module = std::make_unique<ScriptModule>(*this, name);
    ScriptModule* result = module.get();
    modules_.emplace(std::move(name), std::move(module));
    return result;
}

std::unique_ptr<ScriptContext> ScriptEngine::CreateContext() {
    return std::make_unique<ScriptContext>(*this);
}

void ScriptEngine::ForwardDiagnostic(const Diagnostic& diagnostic) const {
    if (messageCallback_) messageCallback_(diagnostic);
}

std::vector<FunctionSignature> ScriptEngine::HostSignatures() const {
    std::vector<FunctionSignature> signatures;
    signatures.reserve(hostFunctions_.size());
    for (const auto& host : hostFunctions_) signatures.push_back(host.signature);
    return signatures;
}

FunctionId ScriptEngine::GetOrCreateFunctionId(std::string key) {
    const auto found = functionIds_.find(key);
    if (found != functionIds_.end()) return found->second;
    const FunctionId id{nextFunctionId_++};
    functionIds_.emplace(std::move(key), id);
    return id;
}

TypeId ScriptEngine::GetOrCreateTypeId(std::string_view name) {
    const auto found = typeIds_.find(std::string(name));
    if (found != typeIds_.end()) return found->second;
    const TypeId id{nextTypeId_++};
    typeIds_.emplace(name, id);
    return id;
}

GlobalId ScriptEngine::GetOrCreateGlobalId(std::string key) {
    const auto found = globalIds_.find(key);
    if (found != globalIds_.end()) return found->second;
    const GlobalId id{nextGlobalId_++};
    globalIds_.emplace(std::move(key), id);
    return id;
}

void ScriptEngine::RegisterModuleImage(const std::shared_ptr<const ModuleImage>& image) {
    for (const auto& function : image->bytecode.functions) moduleImages_[&function] = image;
}

std::shared_ptr<const ModuleImage> ScriptEngine::FindModuleImage(const BytecodeFunction* function) {
    const auto found = moduleImages_.find(function);
    if (found == moduleImages_.end()) return {};
    auto image = found->second.lock();
    if (!image) moduleImages_.erase(found);
    return image;
}

void ScriptEngine::EnqueueFinalizer(ScriptObject* object) {
    if (object) finalizerQueue_.push_back(object);
}

void ScriptEngine::DrainFinalizers() {
    if (drainingFinalizers_) return;
    drainingFinalizers_ = true;
    while (!finalizerQueue_.empty()) {
        ScriptObject* object = finalizerQueue_.front();
        finalizerQueue_.pop_front();
        const ScriptFinalizerBinding binding = object->Finalizer();
        auto state = binding.state.lock();
        for (const FunctionId functionId : binding.functions) {
            const BytecodeFunction* function = binding.module
                ? binding.module->FindFunction(functionId) : nullptr;
            if (!function) {
                ForwardDiagnostic({{"finalizer"}, Severity::Error,
                                   "script destructor target is unavailable"});
                continue;
            }
            VirtualMachine finalizer;
            finalizer.SetFinalizerContext(this, binding.module, binding.state,
                                          [this] { DrainFinalizers(); });
            const ExecutionResult result = finalizer.Execute(
                *function, {Value(ObjectHandle(object))}, binding.module.get(), state.get());
            if (result.state != ExecutionState::Finished) {
                ForwardDiagnostic({result.location, Severity::Error,
                                   "script destructor '" + function->signature.name +
                                   "' failed: " + result.exception});
            }
        }
        object->Release();
    }
    drainingFinalizers_ = false;
}

std::unique_ptr<ScriptEngine> CreateScriptEngine() { return std::make_unique<ScriptEngine>(); }

} // namespace mini_as
