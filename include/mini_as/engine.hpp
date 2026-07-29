#pragma once

#include "mini_as/vm.hpp"
#include "mini_as/generic.hpp"
#include "mini_as/object.hpp"

#include <functional>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mini_as {

std::string_view Version();

enum class ModulePolicy { AlwaysCreate, CreateIfMissing, OnlyIfExists };

class ScriptEngine;

class ScriptModule {
public:
    ScriptModule(ScriptEngine& engine, std::string name);
    const std::string& GetName() const;
    void AddScriptSection(std::string name, std::string source);
    bool Build();
    const BytecodeFunction* GetFunctionByDecl(std::string_view declaration) const;
    const BytecodeFunction* GetFunctionByName(std::string_view name) const;
    const BytecodeModule& Bytecode() const;

private:
    struct Section { std::string name; std::string source; };
    ScriptEngine& engine_;
    std::string name_;
    std::vector<Section> sections_;
    BytecodeModule bytecode_;
};

class ScriptContext {
public:
    explicit ScriptContext(ScriptEngine& engine);
    bool Prepare(const BytecodeFunction* function);
    bool SetArgInt(std::size_t index, std::int32_t value);
    bool SetArgFloat(std::size_t index, float value);
    bool SetArgBool(std::size_t index, bool value);
    bool SetArgString(std::size_t index, std::string value);
    bool SetArgObject(std::size_t index, ObjectHandle value);
    ExecutionState Execute();
    void Suspend();
    void Abort();
    ExecutionState GetState() const;
    const Value& GetReturnValue() const;
    std::int32_t GetReturnInt() const;
    float GetReturnFloat() const;
    const std::string& GetExceptionString() const;
    const SourceLocation& GetExceptionLocation() const;

private:
    bool SetArgument(std::size_t index, Value value);
    ScriptEngine& engine_;
    const BytecodeFunction* function_ = nullptr;
    std::vector<Value> arguments_;
    VirtualMachine vm_;
    ExecutionResult result_;
};

class ScriptEngine {
public:
    using MessageCallback = std::function<void(const Diagnostic&)>;

    void SetMessageCallback(MessageCallback callback);
    bool RegisterGlobalFunction(std::string declaration, GenericFunction callback);
    const TypeInfo* RegisterObjectType(std::string name);
    const TypeInfo* GetTypeInfo(std::string_view name) const;
    std::size_t CollectGarbage();
    std::size_t GetTrackedObjectCount() const;
    ScriptModule* GetModule(std::string name = {},
                            ModulePolicy policy = ModulePolicy::CreateIfMissing);
    std::unique_ptr<ScriptContext> CreateContext();

private:
    friend class ScriptModule;
    void ForwardDiagnostic(const Diagnostic& diagnostic) const;
    std::vector<FunctionSignature> HostSignatures() const;
    const TypeInfo* RegisterScriptType(const ClassSignature& type);
    MessageCallback messageCallback_;
    std::unordered_map<std::string, std::unique_ptr<ScriptModule>> modules_;
    std::deque<RegisteredHostFunction> hostFunctions_;
    GarbageCollector garbageCollector_;
    std::unordered_map<std::string, std::unique_ptr<TypeInfo>> objectTypes_;
};

std::unique_ptr<ScriptEngine> CreateScriptEngine();

} // namespace mini_as
