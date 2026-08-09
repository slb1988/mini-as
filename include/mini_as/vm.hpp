#pragma once

#include "mini_as/bytecode.hpp"

#include <string>

namespace mini_as {

enum class ExecutionState { Uninitialized, Prepared, Active, Suspended, Finished, Aborted, Exception };

struct StackFrameInfo {
    std::string functionDeclaration;
    SourceLocation location;
};

struct ExecutionResult {
    ExecutionState state = ExecutionState::Uninitialized;
    Value returnValue;
    std::string exception;
    SourceLocation location;
    std::vector<StackFrameInfo> callStack;
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
    ExecutionResult Execute(const BytecodeFunction& function,
                            const std::vector<Value>& arguments = {},
                            const BytecodeModule* module = nullptr, ModuleState* state = nullptr);

private:
    struct CallFrame {
        const BytecodeFunction* function = nullptr;
        std::size_t pc = 0;
        std::vector<Value> locals;
    };

    bool Step();
    Value Pop();
    void Push(Value value);
    void Fail(const Instruction& instruction, std::string message);
    void BinaryArithmetic(const Instruction& instruction);
    void Compare(const Instruction& instruction);

    std::vector<Value> stack_;
    std::vector<Value> locals_;
    std::vector<CallFrame> callStack_;
    const BytecodeFunction* function_ = nullptr;
    const BytecodeModule* module_ = nullptr;
    ModuleState* moduleState_ = nullptr;
    std::size_t pc_ = 0;
    bool suspendRequested_ = false;
    std::function<void(const SourceLocation&)> lineCallback_;
    ObjectFinalizerQueue* finalizerQueue_ = nullptr;
    std::shared_ptr<const BytecodeModule> finalizerModule_;
    std::weak_ptr<ModuleState> finalizerState_;
    std::function<void()> safePoint_;
    ExecutionResult result_;
};

} // namespace mini_as
