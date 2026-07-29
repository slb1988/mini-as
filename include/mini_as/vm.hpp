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
    ExecutionResult Execute(const BytecodeFunction& function,
                            const std::vector<Value>& arguments = {});

private:
    bool Step(const BytecodeFunction& function);
    Value Pop();
    void Push(Value value);
    void Fail(const Instruction& instruction, std::string message);
    void BinaryArithmetic(const Instruction& instruction);
    void Compare(const Instruction& instruction);

    std::vector<Value> stack_;
    std::vector<Value> locals_;
    std::size_t pc_ = 0;
    ExecutionResult result_;
};

} // namespace mini_as

