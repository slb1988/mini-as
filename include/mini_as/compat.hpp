#pragma once

#include "mini_as/engine.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mini_as::compat {

inline constexpr int asSUCCESS = 0;
inline constexpr int asERROR = -1;
inline constexpr int asCONTEXT_ACTIVE = -2;
inline constexpr int asCONTEXT_NOT_FINISHED = -3;
inline constexpr int asCONTEXT_NOT_PREPARED = -4;
inline constexpr int asINVALID_ARG = -5;
inline constexpr int asNO_FUNCTION = -6;
inline constexpr int asNOT_SUPPORTED = -7;
inline constexpr int asINVALID_NAME = -8;
inline constexpr int asNAME_TAKEN = -9;
inline constexpr int asINVALID_DECLARATION = -10;
inline constexpr int asINVALID_OBJECT = -11;
inline constexpr int asINVALID_TYPE = -12;
inline constexpr int asALREADY_REGISTERED = -13;
inline constexpr int asMULTIPLE_FUNCTIONS = -14;
inline constexpr int asNO_MODULE = -15;
inline constexpr int asNO_GLOBAL_VAR = -16;
inline constexpr int asINVALID_CONFIGURATION = -17;
inline constexpr int asINVALID_INTERFACE = -18;
inline constexpr int asCANT_BIND_ALL_FUNCTIONS = -19;
inline constexpr int asLOWER_ARRAY_DIMENSION_NOT_REGISTERED = -20;
inline constexpr int asWRONG_CONFIG_GROUP = -21;
inline constexpr int asCONFIG_GROUP_IS_IN_USE = -22;
inline constexpr int asILLEGAL_BEHAVIOUR_FOR_TYPE = -23;
inline constexpr int asWRONG_CALLING_CONV = -24;
inline constexpr int asBUILD_IN_PROGRESS = -25;
inline constexpr int asINIT_GLOBAL_VARS_FAILED = -26;
inline constexpr int asOUT_OF_MEMORY = -27;
inline constexpr int asMODULE_IS_IN_USE = -28;

inline constexpr int asEXECUTION_FINISHED = 0;
inline constexpr int asEXECUTION_SUSPENDED = 1;
inline constexpr int asEXECUTION_ABORTED = 2;
inline constexpr int asEXECUTION_EXCEPTION = 3;
inline constexpr int asEXECUTION_PREPARED = 4;
inline constexpr int asEXECUTION_UNINITIALIZED = 5;
inline constexpr int asEXECUTION_ACTIVE = 6;
inline constexpr int asEXECUTION_ERROR = 7;
inline constexpr int asEXECUTION_DESERIALIZATION = 8;

inline constexpr int asGM_ONLY_IF_EXISTS = 0;
inline constexpr int asGM_CREATE_IF_NOT_EXISTS = 1;
inline constexpr int asGM_ALWAYS_CREATE = 2;
inline constexpr std::uint32_t asCOMP_ADD_TO_MODULE = 1;
inline constexpr std::uint32_t asGC_FULL_CYCLE = 1;
inline constexpr std::uint32_t asGC_ONE_STEP = 2;
inline constexpr std::uint32_t asGC_DESTROY_GARBAGE = 4;
inline constexpr std::uint32_t asGC_DETECT_GARBAGE = 8;

class ScriptEngine;

class ScriptModule {
public:
    const char* GetName() const;
    int AddScriptSection(const char* name, const char* source,
                         std::size_t length = 0, int lineOffset = 0);
    int Build();
    const BytecodeFunction* GetFunctionByDecl(const char* declaration) const;
    const BytecodeFunction* GetFunctionByName(const char* name) const;
    std::size_t GetImportedFunctionCount() const;
    int GetImportedFunctionIndexByDecl(const char* declaration) const;
    const char* GetImportedFunctionDeclaration(std::size_t index) const;
    const char* GetImportedFunctionSourceModule(std::size_t index) const;
    int BindImportedFunction(std::size_t index, const BytecodeFunction* function);
    int BindAllImportedFunctions();
    int UnbindImportedFunction(std::size_t index);
    int UnbindAllImportedFunctions();
    int CompileFunction(const char* sectionName, const char* source, int lineOffset,
                        std::uint32_t flags, const BytecodeFunction** output);
    int RemoveFunction(const BytecodeFunction* function);
    int SaveByteCode(std::ostream& output) const;
    int LoadByteCode(std::istream& input);
    std::uint32_t SetAccessMask(std::uint32_t accessMask);
    std::uint32_t GetAccessMask() const;
    int SetDefaultNamespace(const char* nameSpace);
    const char* GetDefaultNamespace() const;
    mini_as::ScriptModule& Native();
    const mini_as::ScriptModule& Native() const;

private:
    friend class ScriptEngine;
    explicit ScriptModule(mini_as::ScriptModule& module);
    mini_as::ScriptModule* module_ = nullptr;
    mutable std::string importScratch_;
};

class ScriptContext {
public:
    using LineCallback = std::function<void(ScriptContext&, const SourceLocation&)>;
    int Prepare(const BytecodeFunction* function);
    int SetArgDWord(std::size_t index, std::uint32_t value);
    int SetArgQWord(std::size_t index, std::uint64_t value);
    int SetArgFloat(std::size_t index, float value);
    int SetArgDouble(std::size_t index, double value);
    int SetArgBool(std::size_t index, bool value);
    int SetArgString(std::size_t index, std::string value);
    int SetArgObject(std::size_t index, ObjectHandle value);
    int SetArgValue(std::size_t index, Value value);
    int Execute();
    int Suspend();
    int Abort();
    void SetLineCallback(LineCallback callback);
    int GetState() const;
    std::uint32_t GetReturnDWord() const;
    std::uint64_t GetReturnQWord() const;
    float GetReturnFloat() const;
    double GetReturnDouble() const;
    const Value& GetReturnValue() const;
    const char* GetExceptionString() const;
    std::size_t GetCallstackSize() const;
    const BytecodeFunction* GetFunction(std::size_t stackLevel = 0) const;
    int GetLineNumber(std::size_t stackLevel = 0, int* column = nullptr,
                      const char** sectionName = nullptr) const;
    int GetVarCount(std::size_t stackLevel = 0) const;
    int GetVar(std::size_t index, std::size_t stackLevel, const char** name,
               DataType* type = nullptr, bool* inScope = nullptr,
               const Value** value = nullptr) const;
    mini_as::ScriptContext& Native();
    const mini_as::ScriptContext& Native() const;

private:
    friend class ScriptEngine;
    explicit ScriptContext(std::unique_ptr<mini_as::ScriptContext> context);
    static int MapState(ExecutionState state);
    std::unique_ptr<mini_as::ScriptContext> context_;
    mutable std::string sectionScratch_;
    mutable std::vector<LocalVariableInfo> localScratch_;
};

class ScriptEngine {
public:
    using MessageCallback = mini_as::ScriptEngine::MessageCallback;
    using CircularReferenceCallback = mini_as::ScriptEngine::CircularReferenceCallback;

    int SetMessageCallback(MessageCallback callback);
    std::uint32_t SetDefaultAccessMask(std::uint32_t accessMask);
    int SetDefaultNamespace(const char* nameSpace);
    const char* GetDefaultNamespace() const;
    int BeginConfigGroup(const char* name);
    int EndConfigGroup();
    int RemoveConfigGroup(const char* name);
    int RegisterGlobalFunction(const char* declaration, GenericFunction callback);
    int RegisterGlobalProperty(const char* declaration, Value* storage);
    int RegisterEnum(const char* name);
    int RegisterEnumValue(const char* enumName, const char* valueName, std::int32_t value);
    int RegisterTypedef(const char* name, DataType underlyingType);
    int RegisterFuncdef(const char* declaration);
    int GarbageCollect(std::uint32_t flags = asGC_FULL_CYCLE,
                       std::uint32_t numIterations = 1);
    void GetGCStatistics(std::uint32_t* currentSize,
                         std::uint32_t* totalDestroyed = nullptr,
                         std::uint32_t* totalDetected = nullptr,
                         std::uint32_t* newObjects = nullptr,
                         std::uint32_t* totalNewDestroyed = nullptr) const;
    void SetCircularRefDetectedCallback(CircularReferenceCallback callback);
    ScriptModule* GetModule(const char* name = "",
                            int flag = asGM_CREATE_IF_NOT_EXISTS);
    std::unique_ptr<ScriptContext> CreateContext();
    mini_as::ScriptEngine& Native();
    const mini_as::ScriptEngine& Native() const;

private:
    friend std::unique_ptr<ScriptEngine> CreateScriptEngine();
    ScriptEngine();
    std::unique_ptr<mini_as::ScriptEngine> engine_;
    std::unordered_map<std::string, std::unique_ptr<ScriptModule>> modules_;
};

std::unique_ptr<ScriptEngine> CreateScriptEngine();

} // namespace mini_as::compat
