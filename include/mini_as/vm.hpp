#pragma once

#include "mini_as/bytecode.hpp"

#include <string>

namespace mini_as {

enum class ExecutionState { Uninitialized, Prepared, Active, Suspended, Finished, Aborted, Exception };

struct ExecutionResult {
    ExecutionState state = ExecutionState::Uninitialized;
    Value returnValue;
    std::string exception;
    SourceLocation location;
};

class VirtualMachine {
public:
    bool Prepare(const BytecodeFunction& function, const std::vector<Value>& arguments = {});
    ExecutionResult Continue();
    void RequestSuspend();
    void Abort();
    ExecutionResult Execute(const BytecodeFunction& function,
                            const std::vector<Value>& arguments = {});

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
    std::size_t pc_ = 0;
    bool suspendRequested_ = false;
    ExecutionResult result_;
};

} // namespace mini_as
