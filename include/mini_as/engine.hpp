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

struct ModuleImage {
    BytecodeModule bytecode;
    std::shared_ptr<const BytecodeModule> finalizerBytecode;
    std::shared_ptr<ModuleState> state;
};

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
    friend class ScriptEngine;
    struct Section { std::string name; std::string source; };
    ScriptEngine& engine_;
    std::string name_;
    std::vector<Section> sections_;
    std::shared_ptr<const ModuleImage> image_;
};

class ScriptContext {
public:
    using LineCallback = std::function<void(ScriptContext&, const SourceLocation&)>;
    explicit ScriptContext(ScriptEngine& engine);
    bool Prepare(const BytecodeFunction* function);
    bool SetArgInt(std::size_t index, std::int32_t value);
    bool SetArgFloat(std::size_t index, float value);
    bool SetArgDouble(std::size_t index, double value);
    bool SetArgBool(std::size_t index, bool value);
    bool SetArgString(std::size_t index, std::string value);
    bool SetArgObject(std::size_t index, ObjectHandle value);
    ExecutionState Execute();
    void Suspend();
    void Abort();
    void SetLineCallback(LineCallback callback);
    ExecutionState GetState() const;
    const Value& GetReturnValue() const;
    std::int32_t GetReturnInt() const;
    float GetReturnFloat() const;
    double GetReturnDouble() const;
    const std::string& GetExceptionString() const;
    const SourceLocation& GetExceptionLocation() const;
    const std::vector<StackFrameInfo>& GetCallStack() const;

private:
    bool SetArgument(std::size_t index, Value value);
    ScriptEngine& engine_;
    std::shared_ptr<const ModuleImage> image_;
    const BytecodeFunction* function_ = nullptr;
    std::vector<Value> arguments_;
    VirtualMachine vm_;
    ExecutionResult result_;
    LineCallback lineCallback_;
};

class ScriptEngine : private ObjectFinalizerQueue {
public:
    using MessageCallback = std::function<void(const Diagnostic&)>;

    ~ScriptEngine() override;

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
    friend class ScriptContext;
    void ForwardDiagnostic(const Diagnostic& diagnostic) const;
    std::vector<FunctionSignature> HostSignatures() const;
    const TypeInfo* RegisterScriptType(const ClassSignature& type);
    FunctionId GetOrCreateFunctionId(std::string key);
    TypeId GetOrCreateTypeId(std::string_view name);
    GlobalId GetOrCreateGlobalId(std::string key);
    void RegisterModuleImage(const std::shared_ptr<const ModuleImage>& image);
    std::shared_ptr<const ModuleImage> FindModuleImage(const BytecodeFunction* function);
    void EnqueueFinalizer(ScriptObject* object) override;
    void DrainFinalizers();
    std::deque<ScriptObject*> finalizerQueue_;
    bool drainingFinalizers_ = false;
    MessageCallback messageCallback_;
    std::unordered_map<std::string, std::unique_ptr<ScriptModule>> modules_;
    std::deque<RegisteredHostFunction> hostFunctions_;
    GarbageCollector garbageCollector_;
    std::unordered_map<std::string, std::unique_ptr<TypeInfo>> objectTypes_;
    std::unordered_map<std::string, FunctionId> functionIds_;
    std::unordered_map<std::string, TypeId> typeIds_;
    std::unordered_map<std::string, GlobalId> globalIds_;
    std::uint32_t nextFunctionId_ = 0;
    std::uint32_t nextTypeId_ = 0;
    std::uint32_t nextGlobalId_ = 0;
    std::unordered_map<const BytecodeFunction*, std::weak_ptr<const ModuleImage>> moduleImages_;
};

std::unique_ptr<ScriptEngine> CreateScriptEngine();

} // namespace mini_as
