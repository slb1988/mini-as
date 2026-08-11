#include "mini_as/compat.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace mini_as::compat {

ScriptModule::ScriptModule(mini_as::ScriptModule& module) : module_(&module) {}
const char* ScriptModule::GetName() const { return module_->GetName().c_str(); }

int ScriptModule::AddScriptSection(const char* name, const char* source,
                                   std::size_t length, int lineOffset) {
    if (!name || !*name) return asINVALID_NAME;
    if (!source || lineOffset < 0) return asINVALID_ARG;
    const std::size_t sourceLength = length ? length : std::strlen(source);
    std::string adjusted(static_cast<std::size_t>(lineOffset), '\n');
    adjusted.append(source, sourceLength);
    module_->AddScriptSection(name, std::move(adjusted));
    return asSUCCESS;
}

int ScriptModule::Build() { return module_->Build() ? asSUCCESS : asERROR; }
const BytecodeFunction* ScriptModule::GetFunctionByDecl(const char* declaration) const {
    return declaration ? module_->GetFunctionByDecl(declaration) : nullptr;
}
const BytecodeFunction* ScriptModule::GetFunctionByName(const char* name) const {
    return name ? module_->GetFunctionByName(name) : nullptr;
}
std::size_t ScriptModule::GetImportedFunctionCount() const {
    return module_->GetImportedFunctionCount();
}
int ScriptModule::GetImportedFunctionIndexByDecl(const char* declaration) const {
    if (!declaration) return asINVALID_ARG;
    for (std::size_t index = 0; index < module_->GetImportedFunctionCount(); ++index)
        if (module_->GetImportedFunctionDeclaration(index) == declaration)
            return static_cast<int>(index);
    return asNO_FUNCTION;
}
const char* ScriptModule::GetImportedFunctionDeclaration(std::size_t index) const {
    if (index >= module_->GetImportedFunctionCount()) return nullptr;
    importScratch_ = module_->GetImportedFunctionDeclaration(index);
    return importScratch_.c_str();
}
const char* ScriptModule::GetImportedFunctionSourceModule(std::size_t index) const {
    if (index >= module_->GetImportedFunctionCount()) return nullptr;
    importScratch_ = module_->GetImportedFunctionSourceModule(index);
    return importScratch_.c_str();
}
int ScriptModule::BindImportedFunction(std::size_t index,
                                       const BytecodeFunction* function) {
    if (index >= module_->GetImportedFunctionCount()) return asINVALID_ARG;
    if (!function) return asNO_FUNCTION;
    return module_->BindImportedFunction(index, function) ? asSUCCESS : asINVALID_INTERFACE;
}
int ScriptModule::BindAllImportedFunctions() {
    return module_->BindAllImportedFunctions() ? asSUCCESS : asCANT_BIND_ALL_FUNCTIONS;
}
int ScriptModule::UnbindImportedFunction(std::size_t index) {
    return module_->UnbindImportedFunction(index) ? asSUCCESS : asINVALID_ARG;
}
int ScriptModule::UnbindAllImportedFunctions() {
    module_->UnbindAllImportedFunctions();
    return asSUCCESS;
}

int ScriptModule::CompileFunction(const char* sectionName, const char* source, int lineOffset,
                                  std::uint32_t flags, const BytecodeFunction** output) {
    if (output) *output = nullptr;
    if (!sectionName || !*sectionName) return asINVALID_NAME;
    if (!source || lineOffset < 0 || (flags & ~asCOMP_ADD_TO_MODULE) != 0) return asINVALID_ARG;
    const auto* function = module_->CompileFunction(sectionName, source,
        (flags & asCOMP_ADD_TO_MODULE) != 0, lineOffset);
    if (output) *output = function;
    return function ? asSUCCESS : asERROR;
}

int ScriptModule::RemoveFunction(const BytecodeFunction* function) {
    if (!function) return asNO_FUNCTION;
    return module_->RemoveFunction(function) ? asSUCCESS : asNO_FUNCTION;
}
int ScriptModule::SaveByteCode(std::ostream& output) const {
    return module_->SaveBytecode(output) ? asSUCCESS : asERROR;
}
int ScriptModule::LoadByteCode(std::istream& input) {
    return module_->LoadBytecode(input) ? asSUCCESS : asERROR;
}
std::uint32_t ScriptModule::SetAccessMask(std::uint32_t accessMask) {
    return module_->SetAccessMask(accessMask);
}
std::uint32_t ScriptModule::GetAccessMask() const { return module_->GetAccessMask(); }
int ScriptModule::SetDefaultNamespace(const char* nameSpace) {
    return nameSpace && module_->SetDefaultNamespace(nameSpace) ? asSUCCESS : asINVALID_NAME;
}
const char* ScriptModule::GetDefaultNamespace() const {
    return module_->GetDefaultNamespace().c_str();
}
mini_as::ScriptModule& ScriptModule::Native() { return *module_; }
const mini_as::ScriptModule& ScriptModule::Native() const { return *module_; }

ScriptContext::ScriptContext(std::unique_ptr<mini_as::ScriptContext> context)
    : context_(std::move(context)) {}
int ScriptContext::Prepare(const BytecodeFunction* function) {
    return context_->Prepare(function) ? asSUCCESS : asNO_FUNCTION;
}
int ScriptContext::SetArgDWord(std::size_t index, std::uint32_t value) {
    return context_->SetArgInt(index, static_cast<std::int32_t>(value)) ? asSUCCESS : asINVALID_ARG;
}
int ScriptContext::SetArgQWord(std::size_t index, std::uint64_t value) {
    return context_->SetArgValue(index, Value::Integer(DataType::UInt64(), value))
        ? asSUCCESS : asINVALID_ARG;
}
int ScriptContext::SetArgFloat(std::size_t index, float value) {
    return context_->SetArgFloat(index, value) ? asSUCCESS : asINVALID_ARG;
}
int ScriptContext::SetArgDouble(std::size_t index, double value) {
    return context_->SetArgDouble(index, value) ? asSUCCESS : asINVALID_ARG;
}
int ScriptContext::SetArgBool(std::size_t index, bool value) {
    return context_->SetArgBool(index, value) ? asSUCCESS : asINVALID_ARG;
}
int ScriptContext::SetArgString(std::size_t index, std::string value) {
    return context_->SetArgString(index, std::move(value)) ? asSUCCESS : asINVALID_ARG;
}
int ScriptContext::SetArgObject(std::size_t index, ObjectHandle value) {
    return context_->SetArgObject(index, std::move(value)) ? asSUCCESS : asINVALID_ARG;
}
int ScriptContext::SetArgValue(std::size_t index, Value value) {
    return context_->SetArgValue(index, std::move(value)) ? asSUCCESS : asINVALID_ARG;
}
int ScriptContext::Execute() {
    const auto state = context_->GetState();
    if (state != ExecutionState::Prepared && state != ExecutionState::Suspended)
        return asCONTEXT_NOT_PREPARED;
    return MapState(context_->Execute());
}
int ScriptContext::Suspend() { context_->Suspend(); return asSUCCESS; }
int ScriptContext::Abort() { context_->Abort(); return asSUCCESS; }
void ScriptContext::SetLineCallback(LineCallback callback) {
    context_->SetLineCallback([this, callback = std::move(callback)](
        mini_as::ScriptContext&, const SourceLocation& location) {
        if (callback) callback(*this, location);
    });
}
int ScriptContext::GetState() const { return MapState(context_->GetState()); }
std::uint32_t ScriptContext::GetReturnDWord() const {
    return static_cast<std::uint32_t>(context_->GetReturnValue().UnsignedInteger());
}
std::uint64_t ScriptContext::GetReturnQWord() const {
    return context_->GetReturnValue().UnsignedInteger();
}
float ScriptContext::GetReturnFloat() const { return context_->GetReturnFloat(); }
double ScriptContext::GetReturnDouble() const { return context_->GetReturnDouble(); }
const Value& ScriptContext::GetReturnValue() const { return context_->GetReturnValue(); }
const char* ScriptContext::GetExceptionString() const {
    return context_->GetExceptionString().c_str();
}
std::size_t ScriptContext::GetCallstackSize() const { return context_->GetCallStackSize(); }
const BytecodeFunction* ScriptContext::GetFunction(std::size_t stackLevel) const {
    return context_->GetFunction(stackLevel);
}
int ScriptContext::GetLineNumber(std::size_t stackLevel, int* column,
                                 const char** sectionName) const {
    if (stackLevel >= context_->GetCallStackSize()) return asINVALID_ARG;
    const auto location = context_->GetInstructionLocation(stackLevel);
    if (column) *column = static_cast<int>(location.column);
    if (sectionName) {
        sectionScratch_ = location.section;
        *sectionName = sectionScratch_.c_str();
    }
    return static_cast<int>(location.row);
}
int ScriptContext::GetVarCount(std::size_t stackLevel) const {
    if (stackLevel >= context_->GetCallStackSize()) return asINVALID_ARG;
    const auto locals = context_->GetLocals(stackLevel);
    if (locals.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return asERROR;
    return static_cast<int>(locals.size());
}
int ScriptContext::GetVar(std::size_t index, std::size_t stackLevel, const char** name,
                          DataType* type, bool* inScope, const Value** value) const {
    if (stackLevel >= context_->GetCallStackSize()) return asINVALID_ARG;
    localScratch_ = context_->GetLocals(stackLevel);
    if (index >= localScratch_.size()) return asINVALID_ARG;
    const auto& local = localScratch_[index];
    if (name) *name = local.name.c_str();
    if (type) *type = local.type;
    if (inScope) *inScope = local.inScope;
    if (value) *value = &local.value;
    return asSUCCESS;
}
mini_as::ScriptContext& ScriptContext::Native() { return *context_; }
const mini_as::ScriptContext& ScriptContext::Native() const { return *context_; }

int ScriptContext::MapState(ExecutionState state) {
    switch (state) {
    case ExecutionState::Finished: return asEXECUTION_FINISHED;
    case ExecutionState::Suspended: return asEXECUTION_SUSPENDED;
    case ExecutionState::Aborted: return asEXECUTION_ABORTED;
    case ExecutionState::Exception: return asEXECUTION_EXCEPTION;
    case ExecutionState::Prepared: return asEXECUTION_PREPARED;
    case ExecutionState::Uninitialized: return asEXECUTION_UNINITIALIZED;
    case ExecutionState::Active: return asEXECUTION_ACTIVE;
    }
    return asEXECUTION_ERROR;
}

ScriptEngine::ScriptEngine() : engine_(mini_as::CreateScriptEngine()) {}
int ScriptEngine::SetMessageCallback(MessageCallback callback) {
    engine_->SetMessageCallback(std::move(callback));
    return asSUCCESS;
}
std::uint32_t ScriptEngine::SetDefaultAccessMask(std::uint32_t accessMask) {
    return engine_->SetDefaultAccessMask(accessMask);
}
int ScriptEngine::SetDefaultNamespace(const char* nameSpace) {
    return nameSpace && engine_->SetDefaultNamespace(nameSpace) ? asSUCCESS : asINVALID_NAME;
}
const char* ScriptEngine::GetDefaultNamespace() const {
    return engine_->GetDefaultNamespace().c_str();
}
int ScriptEngine::BeginConfigGroup(const char* name) {
    return name && engine_->BeginConfigGroup(name) ? asSUCCESS : asINVALID_ARG;
}
int ScriptEngine::EndConfigGroup() {
    return engine_->EndConfigGroup() ? asSUCCESS : asERROR;
}
int ScriptEngine::RemoveConfigGroup(const char* name) {
    return name && engine_->RemoveConfigGroup(name) ? asSUCCESS : asCONFIG_GROUP_IS_IN_USE;
}
int ScriptEngine::RegisterGlobalFunction(const char* declaration, GenericFunction callback) {
    if (!declaration || !*declaration) return asINVALID_DECLARATION;
    if (!callback) return asINVALID_ARG;
    return engine_->RegisterGlobalFunction(declaration, std::move(callback))
        ? asSUCCESS : asINVALID_DECLARATION;
}
int ScriptEngine::RegisterGlobalProperty(const char* declaration, Value* storage) {
    if (!declaration || !*declaration || !storage) return asINVALID_ARG;
    return engine_->RegisterGlobalProperty(declaration, storage)
        ? asSUCCESS : asINVALID_DECLARATION;
}
int ScriptEngine::RegisterEnum(const char* name) {
    if (!name || !*name) return asINVALID_NAME;
    return engine_->RegisterEnum(name) ? asSUCCESS : asINVALID_NAME;
}
int ScriptEngine::RegisterEnumValue(const char* enumName, const char* valueName,
                                    std::int32_t value) {
    if (!enumName || !*enumName || !valueName || !*valueName) return asINVALID_NAME;
    return engine_->RegisterEnumValue(enumName, valueName, value)
        ? asSUCCESS : asINVALID_NAME;
}
int ScriptEngine::RegisterTypedef(const char* name, DataType underlyingType) {
    if (!name || !*name) return asINVALID_NAME;
    return engine_->RegisterTypedef(name, std::move(underlyingType))
        ? asSUCCESS : asINVALID_TYPE;
}
int ScriptEngine::RegisterFuncdef(const char* declaration) {
    if (!declaration || !*declaration) return asINVALID_DECLARATION;
    return engine_->RegisterFuncdef(declaration) ? asSUCCESS : asINVALID_DECLARATION;
}
ScriptModule* ScriptEngine::GetModule(const char* name, int flag) {
    if (!name) return nullptr;
    ModulePolicy policy;
    if (flag == asGM_ONLY_IF_EXISTS) policy = ModulePolicy::OnlyIfExists;
    else if (flag == asGM_CREATE_IF_NOT_EXISTS) policy = ModulePolicy::CreateIfMissing;
    else if (flag == asGM_ALWAYS_CREATE) policy = ModulePolicy::AlwaysCreate;
    else return nullptr;
    const auto wrapped = modules_.find(name);
    if (policy != ModulePolicy::AlwaysCreate && wrapped != modules_.end())
        return wrapped->second.get();
    auto* native = engine_->GetModule(name, policy);
    if (!native) return nullptr;
    auto wrapper = std::unique_ptr<ScriptModule>(new ScriptModule(*native));
    auto* result = wrapper.get();
    modules_[name] = std::move(wrapper);
    return result;
}
std::unique_ptr<ScriptContext> ScriptEngine::CreateContext() {
    return std::unique_ptr<ScriptContext>(new ScriptContext(engine_->CreateContext()));
}
mini_as::ScriptEngine& ScriptEngine::Native() { return *engine_; }
const mini_as::ScriptEngine& ScriptEngine::Native() const { return *engine_; }
std::unique_ptr<ScriptEngine> CreateScriptEngine() {
    return std::unique_ptr<ScriptEngine>(new ScriptEngine());
}

} // namespace mini_as::compat
