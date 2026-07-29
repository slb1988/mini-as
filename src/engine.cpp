#include "mini_as/engine.hpp"

#include <utility>

namespace mini_as {

std::string_view Version() { return "0.1.0-learning"; }

ScriptModule::ScriptModule(ScriptEngine& engine, std::string name)
    : engine_(engine), name_(std::move(name)) {}

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
    if (!typed) { bytecode_ = {}; return false; }
    BytecodeCompiler compiler(diagnostics);
    BytecodeModule candidate = compiler.Compile(tree.root, checker.Functions());
    if (diagnostics.HasErrors()) { bytecode_ = {}; return false; }
    for (auto& function : candidate.functions) {
        for (const auto& host : engine_.hostFunctions_) function.hostTargets.push_back(&host);
    }
    bytecode_ = std::move(candidate);
    sections_.clear();
    return true;
}

const BytecodeFunction* ScriptModule::GetFunctionByDecl(std::string_view declaration) const {
    for (const auto& function : bytecode_.functions) {
        if (function.signature.Declaration() == declaration) return &function;
    }
    return nullptr;
}

const BytecodeFunction* ScriptModule::GetFunctionByName(std::string_view name) const {
    for (const auto& function : bytecode_.functions) if (function.signature.name == name) return &function;
    return nullptr;
}

const BytecodeModule& ScriptModule::Bytecode() const { return bytecode_; }

ScriptContext::ScriptContext(ScriptEngine& engine) : engine_(engine) {}

bool ScriptContext::Prepare(const BytecodeFunction* function) {
    function_ = function;
    if (!function_) {
        result_ = {ExecutionState::Exception, {}, "cannot prepare a null function"};
        return false;
    }
    arguments_.assign(function_->signature.parameters.size(), Value{});
    result_ = {ExecutionState::Prepared};
    return true;
}

bool ScriptContext::SetArgInt(std::size_t index, std::int32_t value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgFloat(std::size_t index, float value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgBool(std::size_t index, bool value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgString(std::size_t index, std::string value) { return SetArgument(index, Value(std::move(value))); }
bool ScriptContext::SetArgObject(std::size_t index, ObjectHandle value) { return SetArgument(index, Value(std::move(value))); }

ExecutionState ScriptContext::Execute() {
    if (!function_) {
        result_ = {ExecutionState::Exception, {}, "context has no prepared function"};
        return result_.state;
    }
    if (result_.state == ExecutionState::Prepared) {
        if (!vm_.Prepare(*function_, arguments_)) {
            result_ = vm_.Continue();
            return result_.state;
        }
    }
    result_ = vm_.Continue();
    return result_.state;
}

void ScriptContext::Suspend() { vm_.RequestSuspend(); }
void ScriptContext::Abort() { vm_.Abort(); result_.state = ExecutionState::Aborted; }
ExecutionState ScriptContext::GetState() const { return result_.state; }
const Value& ScriptContext::GetReturnValue() const { return result_.returnValue; }
std::int32_t ScriptContext::GetReturnInt() const { return result_.returnValue.As<std::int32_t>(); }
float ScriptContext::GetReturnFloat() const { return result_.returnValue.As<float>(); }
const std::string& ScriptContext::GetExceptionString() const { return result_.exception; }
const SourceLocation& ScriptContext::GetExceptionLocation() const { return result_.location; }

bool ScriptContext::SetArgument(std::size_t index, Value value) {
    if (!function_ || index >= arguments_.size() || result_.state != ExecutionState::Prepared) return false;
    const DataType expected = function_->signature.parameters[index];
    if (value.Type() == DataType::Int() && expected == DataType::Float()) value = Value(static_cast<float>(value.As<std::int32_t>()));
    else if (value.Type() != expected) return false;
    arguments_[index] = std::move(value);
    return true;
}

void ScriptEngine::SetMessageCallback(MessageCallback callback) { messageCallback_ = std::move(callback); }

bool ScriptEngine::RegisterGlobalFunction(std::string declaration, GenericFunction callback) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    auto signature = ParseFunctionDeclaration(declaration, diagnostics);
    if (!signature || !callback) return false;
    for (const auto& existing : hostFunctions_) {
        if (existing.signature.Declaration() == signature->Declaration()) {
            diagnostics.Report({"registration"}, Severity::Error,
                               "duplicate global function '" + signature->Declaration() + "'");
            return false;
        }
    }
    hostFunctions_.push_back({std::move(*signature), std::move(callback)});
    return true;
}

const TypeInfo* ScriptEngine::RegisterObjectType(std::string name) {
    if (name.empty() || objectTypes_.find(name) != objectTypes_.end()) return nullptr;
    auto type = std::make_unique<TypeInfo>();
    type->name = name;
    const TypeInfo* result = type.get();
    objectTypes_.emplace(std::move(name), std::move(type));
    return result;
}

const TypeInfo* ScriptEngine::GetTypeInfo(std::string_view name) const {
    const auto found = objectTypes_.find(std::string(name));
    return found == objectTypes_.end() ? nullptr : found->second.get();
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

std::unique_ptr<ScriptEngine> CreateScriptEngine() { return std::make_unique<ScriptEngine>(); }

} // namespace mini_as
