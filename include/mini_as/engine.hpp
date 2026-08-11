#pragma once

#include "mini_as/vm.hpp"
#include "mini_as/generic.hpp"
#include "mini_as/object.hpp"

#include <functional>
#include <deque>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace mini_as {

std::string_view Version();

enum class ModulePolicy { AlwaysCreate, CreateIfMissing, OnlyIfExists };

enum class TypeMetadataKind { Object, Enum, Typedef, Funcdef };

struct TypeMetadata {
    TypeId id;
    std::string name;
    TypeMetadataKind kind = TypeMetadataKind::Object;
    bool host = false;
    bool valueType = false;
    bool interfaceType = false;
    bool shared = false;
    bool templateType = false;
    bool templateInstance = false;
    std::string templateBase;
    std::vector<std::string> templateParameters;
    std::vector<DataType> templateSubTypes;
    std::string baseClass;
    std::vector<std::string> interfaces;
    std::vector<FieldSignature> fields;
    std::vector<FunctionSignature> methods;
    std::vector<EnumValueSignature> enumValues;
    DataType underlyingType = DataType::Invalid();
    FunctionSignature funcdef;
};

struct FunctionMetadata {
    FunctionId id;
    std::string moduleName;
    FunctionSignature signature;
};

struct GlobalMetadata {
    GlobalId id;
    std::string moduleName;
    GlobalSignature signature;
};

class ScriptEngine;

struct ModuleCompilationEnvironment {
    std::vector<FunctionSignature> functions;
    std::vector<ClassSignature> classes;
    std::vector<GlobalSignature> globals;
    std::vector<EnumSignature> enums;
    std::vector<TypedefSignature> typedefs;
    std::vector<FuncdefSignature> funcdefs;
};

struct ModuleImage {
    BytecodeModule bytecode;
    std::shared_ptr<const BytecodeModule> finalizerBytecode;
    std::shared_ptr<ModuleState> state;
    ModuleCompilationEnvironment environment;
    std::vector<std::shared_ptr<const SyntaxTree>> definitionTrees;
    std::vector<FunctionId> removedFunctions;
};

class ScriptModule {
public:
    ScriptModule(ScriptEngine& engine, std::string name);
    const std::string& GetName() const;
    void AddScriptSection(std::string name, std::string source);
    bool Build();
    const BytecodeFunction* GetFunctionByDecl(std::string_view declaration) const;
    const BytecodeFunction* GetFunctionByName(std::string_view name) const;
    const BytecodeFunction* CompileFunction(std::string sectionName, std::string source,
                                            bool addToModule = true, int lineOffset = 0);
    bool RemoveFunction(const BytecodeFunction* function);
    bool SaveBytecode(std::ostream& output) const;
    bool LoadBytecode(std::istream& input);
    bool SaveState(std::ostream& output) const;
    bool LoadState(std::istream& input);
    const FunctionMetadata* GetFunctionMetadataByDecl(std::string_view declaration) const;
    std::size_t GetGlobalMetadataCount() const;
    const GlobalMetadata* GetGlobalMetadataByIndex(std::size_t index) const;
    const GlobalMetadata* GetGlobalMetadataById(GlobalId id) const;
    const GlobalMetadata* GetGlobalMetadataByName(std::string_view name) const;
    const GlobalMetadata* GetGlobalMetadataByDecl(std::string_view declaration) const;
    std::size_t GetImportedFunctionCount() const;
    std::string GetImportedFunctionDeclaration(std::size_t index) const;
    std::string_view GetImportedFunctionSourceModule(std::size_t index) const;
    bool BindImportedFunction(std::size_t index, const BytecodeFunction* function);
    bool BindImportedFunction(std::size_t index, const FunctionMetadata* function);
    bool BindAllImportedFunctions();
    bool UnbindImportedFunction(std::size_t index);
    void UnbindAllImportedFunctions();
    const BytecodeModule& Bytecode() const;
    std::uint32_t SetAccessMask(std::uint32_t accessMask);
    std::uint32_t GetAccessMask() const;
    bool SetDefaultNamespace(std::string nameSpace);
    const std::string& GetDefaultNamespace() const;

private:
    friend class ScriptEngine;
    struct Section { std::string name; std::string source; };
    ScriptEngine& engine_;
    std::string name_;
    std::vector<Section> sections_;
    std::shared_ptr<const ModuleImage> image_;
    std::vector<std::shared_ptr<const ModuleImage>> dynamicImages_;
    std::uint64_t nextDynamicFunctionSerial_ = 0;
    std::uint32_t accessMask_ = ~std::uint32_t{0};
    std::string defaultNamespace_;
};

class ScriptContext {
public:
    using LineCallback = std::function<void(ScriptContext&, const SourceLocation&)>;
    explicit ScriptContext(ScriptEngine& engine);
    bool Prepare(const BytecodeFunction* function);
    bool Unprepare();
    bool SetArgInt(std::size_t index, std::int32_t value);
    bool SetArgFloat(std::size_t index, float value);
    bool SetArgDouble(std::size_t index, double value);
    bool SetArgBool(std::size_t index, bool value);
    bool SetArgString(std::size_t index, std::string value);
    bool SetArgObject(std::size_t index, ObjectHandle value);
    bool SetArgValue(std::size_t index, Value value);
    bool SaveState(std::ostream& output) const;
    bool LoadState(std::istream& input);
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
    std::size_t GetCallStackSize() const;
    const BytecodeFunction* GetFunction(std::size_t stackLevel = 0) const;
    SourceLocation GetInstructionLocation(std::size_t stackLevel = 0) const;
    std::vector<LocalVariableInfo> GetLocals(std::size_t stackLevel = 0) const;

private:
    void ConfigureVirtualMachine();
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
    using GarbageCollectionStatistics = GarbageCollector::Statistics;
    using CircularReferenceCallback = GarbageCollector::CircularReferenceCallback;
    using RequestContextCallback = std::function<ScriptContext*(ScriptEngine&)>;
    using ReturnContextCallback = std::function<void(ScriptEngine&, ScriptContext*)>;
    using TemplateValidator =
        std::function<bool(const std::vector<DataType>&, std::string&)>;
    using TemplateInstanceCallback =
        std::function<bool(ScriptEngine&, const TypeInfo&, std::string&)>;

    ~ScriptEngine() override;

    void SetMessageCallback(MessageCallback callback);
    std::uint32_t SetDefaultAccessMask(std::uint32_t accessMask);
    std::uint32_t GetDefaultAccessMask() const;
    bool SetDefaultNamespace(std::string nameSpace);
    const std::string& GetDefaultNamespace() const;
    bool BeginConfigGroup(std::string name);
    bool EndConfigGroup();
    bool RemoveConfigGroup(std::string_view name);
    bool RegisterGlobalFunction(std::string declaration, GenericFunction callback);
    bool RegisterGlobalProperty(std::string declaration, Value* storage);
    const TypeInfo* RegisterObjectType(std::string name, bool garbageCollected = false);
    const TypeInfo* RegisterTemplateType(std::string declaration,
                                         TemplateValidator validator = {},
                                         TemplateInstanceCallback instanceCallback = {},
                                         bool garbageCollected = false);
    const TypeInfo* RegisterValueType(std::string name, Value defaultValue);
    bool RegisterEnum(std::string name);
    bool RegisterEnumValue(std::string enumName, std::string valueName, std::int32_t value);
    bool RegisterTypedef(std::string name, DataType underlyingType);
    bool RegisterFuncdef(std::string declaration);
    bool RegisterObjectFactory(std::string typeName, std::string declaration,
                               GenericFunction callback);
    bool RegisterObjectMethod(std::string typeName, std::string declaration,
                              GenericFunction callback);
    bool RegisterObjectProperty(std::string typeName, std::string declaration,
                                GenericPropertyGetter getter,
                                GenericPropertySetter setter = {});
    const TypeInfo* GetTypeInfo(std::string_view name) const;
    std::size_t GetTypeMetadataCount() const;
    const TypeMetadata* GetTypeMetadataByIndex(std::size_t index) const;
    const TypeMetadata* GetTypeMetadataById(TypeId id) const;
    const TypeMetadata* GetTypeMetadataByName(std::string_view name) const;
    std::size_t GetFunctionMetadataCount() const;
    const FunctionMetadata* GetFunctionMetadataByIndex(std::size_t index) const;
    const FunctionMetadata* GetFunctionMetadataById(FunctionId id) const;
    std::size_t CollectGarbage();
    std::size_t CollectGarbageStep(std::size_t workBudget = 1);
    bool IsGarbageCollectionInProgress() const;
    std::size_t GetTrackedObjectCount() const;
    GarbageCollectionStatistics GetGarbageCollectionStatistics() const;
    void SetCircularReferenceDetectedCallback(CircularReferenceCallback callback);
    ScriptModule* GetModule(std::string name = {},
                            ModulePolicy policy = ModulePolicy::CreateIfMissing);
    std::unique_ptr<ScriptContext> CreateContext();
    ScriptContext* RequestContext();
    void ReturnContext(ScriptContext* context);
    bool SetContextCallbacks(RequestContextCallback request,
                             ReturnContextCallback release);

private:
    friend class ScriptModule;
    friend class ScriptContext;
    void ForwardDiagnostic(const Diagnostic& diagnostic) const;
    std::vector<FunctionSignature> HostSignatures(std::uint32_t accessMask) const;
    std::vector<GlobalSignature> HostPropertySignatures(std::uint32_t accessMask) const;
    std::vector<ClassSignature> HostTypeSignatures(std::uint32_t accessMask) const;
    std::vector<EnumSignature> HostEnums(std::uint32_t accessMask) const;
    std::vector<TypedefSignature> HostTypedefs(std::uint32_t accessMask) const;
    std::vector<FuncdefSignature> HostFuncdefs(std::uint32_t accessMask) const;
    std::vector<std::pair<std::string, std::size_t>> HostTemplateTypes(
        std::uint32_t accessMask) const;
    std::vector<std::pair<std::string, std::size_t>> HostTemplateFunctions(
        std::uint32_t accessMask) const;
    bool InstantiateTemplateTypes(const std::vector<TemplateTypeUse>& uses,
                                  std::uint32_t accessMask,
                                  DiagnosticSink& diagnostics);
    bool InstantiateTemplateFunctions(const std::vector<TemplateFunctionUse>& uses,
                                      std::uint32_t accessMask,
                                      DiagnosticSink& diagnostics);
    DataType ResolveRegisteredType(DataType type) const;
    void ResolveRegisteredTypes(FunctionSignature& signature) const;
    bool HasRegisteredType(std::string_view name) const;
    void PublishTypeMetadata(TypeMetadata metadata);
    void PublishObjectMetadata(const ClassSignature& signature);
    void PublishEnumMetadata(const EnumSignature& signature, bool host);
    void PublishTypedefMetadata(const TypedefSignature& signature, bool host);
    void PublishFuncdefMetadata(const FuncdefSignature& signature, bool host);
    void PublishFunctionMetadata(FunctionSignature signature, std::string moduleName = {});
    void PublishGlobalMetadata(GlobalSignature signature, std::string moduleName);
    const GlobalMetadata* FindGlobalMetadata(GlobalId id) const;
    const TypeInfo* RegisterScriptType(const ClassSignature& type);
    void LinkScriptType(const ClassSignature& type);
    FunctionId GetOrCreateFunctionId(std::string key);
    TypeId GetOrCreateTypeId(std::string_view name);
    GlobalId GetOrCreateGlobalId(std::string key);
    void RegisterModuleImage(const std::shared_ptr<const ModuleImage>& image);
    std::shared_ptr<const ModuleImage> FindModuleImage(const BytecodeFunction* function);
    std::shared_ptr<const ModuleImage> FindCurrentModuleImage(std::string_view moduleName);
    std::string FindModuleName(const ModuleImage* image) const;
    std::optional<ResolvedScriptFunction> ResolveScriptFunction(FunctionId function);
    void EnqueueFinalizer(ScriptObject* object) override;
    void DrainFinalizers();
    std::deque<ScriptObject*> finalizerQueue_;
    bool drainingFinalizers_ = false;
    MessageCallback messageCallback_;
    std::unordered_map<std::string, std::unique_ptr<ScriptModule>> modules_;
    std::deque<RegisteredHostFunction> hostFunctions_;
    std::deque<RegisteredHostProperty> hostProperties_;
    std::deque<RegisteredHostObjectProperty> hostObjectProperties_;
    std::vector<EnumSignature> hostEnums_;
    std::vector<TypedefSignature> hostTypedefs_;
    std::vector<FuncdefSignature> hostFuncdefs_;
    std::deque<TypeMetadata> typeMetadata_;
    std::deque<FunctionMetadata> functionMetadata_;
    std::deque<GlobalMetadata> globalMetadata_;
    GarbageCollector garbageCollector_;
    std::unordered_map<std::string, std::unique_ptr<TypeInfo>> objectTypes_;
    struct RegisteredTemplateType {
        std::string name;
        std::vector<std::string> parameters;
        TypeInfo* definition = nullptr;
        TemplateValidator validator;
        TemplateInstanceCallback instanceCallback;
        bool garbageCollected = false;
    };
    std::vector<RegisteredTemplateType> templateTypes_;
    std::unordered_map<std::string, FunctionId> functionIds_;
    std::unordered_map<std::string, TypeId> typeIds_;
    std::unordered_map<std::string, GlobalId> globalIds_;
    std::unordered_map<std::string, std::string> sharedEntityFingerprints_;
    std::unordered_map<std::string, ClassSignature> sharedClasses_;
    std::unordered_map<std::string, EnumSignature> sharedEnums_;
    std::unordered_map<std::string, FuncdefSignature> sharedFuncdefs_;
    std::unordered_map<std::string, FunctionSignature> sharedFunctions_;
    std::unordered_map<std::string, std::shared_ptr<const SyntaxTree>> sharedEntityDefinitions_;
    std::uint32_t nextFunctionId_ = 0;
    std::uint32_t nextTypeId_ = 0;
    std::uint32_t nextGlobalId_ = 0;
    std::unordered_map<const BytecodeFunction*, std::weak_ptr<const ModuleImage>> moduleImages_;
    struct RegistrationControl {
        std::uint32_t accessMask = ~std::uint32_t{0};
        std::string configGroup;
        bool active = true;
    };
    std::unordered_map<std::uint32_t, RegistrationControl> typeControls_;
    std::uint32_t defaultAccessMask_ = ~std::uint32_t{0};
    std::string defaultNamespace_;
    std::string currentConfigGroup_;
    std::unordered_set<std::string> configGroups_;
    RequestContextCallback requestContextCallback_;
    ReturnContextCallback returnContextCallback_;
};

std::unique_ptr<ScriptEngine> CreateScriptEngine();

} // namespace mini_as
