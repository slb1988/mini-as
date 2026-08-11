#pragma once

#include "mini_as/bytecode.hpp"

#include <string>
#include <memory>
#include <optional>

namespace mini_as {

enum class ExecutionState { Uninitialized, Prepared, Active, Suspended, Finished, Aborted, Exception };

struct LocalVariableInfo {
    std::string name;
    DataType type = DataType::Invalid();
    VariableId slot;
    Value value;
    bool isConst = false;
    bool parameter = false;
    bool inScope = false;
};

struct StackFrameInfo {
    std::string functionDeclaration;
    SourceLocation location;
    const BytecodeFunction* function = nullptr;
    std::size_t instructionOffset = 0;
    std::vector<LocalVariableInfo> locals;
};

struct ExecutionResult {
    ExecutionState state = ExecutionState::Uninitialized;
    Value returnValue;
    std::string exception;
    SourceLocation location;
    std::vector<StackFrameInfo> callStack;
};

struct ResolvedScriptFunction {
    const BytecodeFunction* function = nullptr;
    const BytecodeModule* module = nullptr;
    ModuleState* state = nullptr;
    std::shared_ptr<const void> owner;
    std::shared_ptr<const BytecodeModule> finalizerModule;
    std::weak_ptr<ModuleState> finalizerState;
};

class VirtualMachine {
public:
    bool Prepare(const BytecodeFunction& function, const std::vector<Value>& arguments = {},
                 const BytecodeModule* module = nullptr, ModuleState* state = nullptr);
    ExecutionResult Continue();
    void RequestSuspend();
    void Abort();
    void SetLineCallback(std::function<void(const SourceLocation&)> callback);
    void SetFinalizerContext(ObjectFinalizerQueue* queue,
                             std::shared_ptr<const BytecodeModule> module,
                             std::weak_ptr<ModuleState> state,
                             std::function<void()> safePoint);
    void SetModuleOwner(std::shared_ptr<const void> owner);
    void SetScriptFunctionResolver(
        std::function<std::optional<ResolvedScriptFunction>(FunctionId)> resolver);
    std::size_t GetCallStackSize() const;
    const BytecodeFunction* GetFunction(std::size_t stackLevel = 0) const;
    SourceLocation GetInstructionLocation(std::size_t stackLevel = 0) const;
    std::vector<LocalVariableInfo> GetLocals(std::size_t stackLevel = 0) const;
    ExecutionResult Execute(const BytecodeFunction& function,
                            const std::vector<Value>& arguments = {},
                            const BytecodeModule* module = nullptr, ModuleState* state = nullptr);

private:
    struct CallFrame {
        CallFrame(const BytecodeFunction* function = nullptr, std::size_t pc = 0,
                  std::vector<Value> locals = {}, std::size_t stackBase = 0,
                  std::vector<CapturedCellHandle> captures = {},
                  const BytecodeModule* module = nullptr, ModuleState* state = nullptr,
                  std::shared_ptr<const void> owner = {},
                  std::shared_ptr<const BytecodeModule> finalizerModule = {},
                  std::weak_ptr<ModuleState> finalizerState = {})
            : function(function), pc(pc), locals(std::move(locals)), stackBase(stackBase),
              captures(std::move(captures)), module(module), state(state),
              owner(std::move(owner)), finalizerModule(std::move(finalizerModule)),
              finalizerState(std::move(finalizerState)) {}

        const BytecodeFunction* function = nullptr;
        std::size_t pc = 0;
        std::vector<Value> locals;
        std::size_t stackBase = 0;
        std::vector<CapturedCellHandle> captures;
        const BytecodeModule* module = nullptr;
        ModuleState* state = nullptr;
        std::shared_ptr<const void> owner;
        std::shared_ptr<const BytecodeModule> finalizerModule;
        std::weak_ptr<ModuleState> finalizerState;
    };

    bool Step();
    Value Pop();
    void Push(Value value);
    void Fail(const Instruction& instruction, std::string message);
    bool HandleException(const Instruction& instruction, std::string message);
    void BinaryArithmetic(const Instruction& instruction);
    void Compare(const Instruction& instruction);
    StackFrameInfo MakeStackFrame(const BytecodeFunction* function, std::size_t pc,
                                  const std::vector<Value>& locals) const;

    std::vector<Value> stack_;
    std::vector<Value> locals_;
    std::vector<CapturedCellHandle> captures_;
    std::vector<CallFrame> callStack_;
    const BytecodeFunction* function_ = nullptr;
    const BytecodeModule* module_ = nullptr;
    ModuleState* moduleState_ = nullptr;
    std::size_t pc_ = 0;
    std::size_t stackBase_ = 0;
    bool suspendRequested_ = false;
    std::function<void(const SourceLocation&)> lineCallback_;
    ObjectFinalizerQueue* finalizerQueue_ = nullptr;
    std::shared_ptr<const BytecodeModule> finalizerModule_;
    std::weak_ptr<ModuleState> finalizerState_;
    std::function<void()> safePoint_;
    std::shared_ptr<const void> moduleOwner_;
    std::function<std::optional<ResolvedScriptFunction>(FunctionId)> scriptFunctionResolver_;
    ExecutionResult result_;
};

} // namespace mini_as
