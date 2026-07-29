#include "mini_as/vm.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace mini_as {
namespace {

float AsFloat(const Value& value) {
    if (value.Type() == DataType::Float()) return value.As<float>();
    if (value.Type() == DataType::Int()) return static_cast<float>(value.As<std::int32_t>());
    throw std::runtime_error("expected numeric value");
}

bool ValuesEqual(const Value& left, const Value& right) {
    if (left.Type().IsNumeric() && right.Type().IsNumeric()) return AsFloat(left) == AsFloat(right);
    return left == right;
}

} // namespace

ExecutionResult VirtualMachine::Execute(const BytecodeFunction& function,
                                        const std::vector<Value>& arguments) {
    stack_.clear();
    locals_.assign(function.localCount, Value{});
    pc_ = 0;
    result_ = {ExecutionState::Active};
    if (arguments.size() != function.signature.parameters.size()) {
        result_.state = ExecutionState::Exception;
        result_.exception = "argument count does not match function signature";
        return result_;
    }
    for (std::size_t i = 0; i < arguments.size(); ++i) locals_[i] = arguments[i];
    while (result_.state == ExecutionState::Active) {
        if (pc_ >= function.code.size()) {
            result_.state = ExecutionState::Exception;
            result_.exception = "program counter escaped function";
            break;
        }
        try { Step(function); }
        catch (const std::exception& error) {
            Fail(function.code[pc_ ? pc_ - 1 : 0], error.what());
        }
    }
    return result_;
}

bool VirtualMachine::Step(const BytecodeFunction& function) {
    const Instruction instruction = function.code[pc_++];
    const auto slot = [&]() -> std::size_t {
        if (instruction.operand < 0) throw std::runtime_error("negative local slot");
        return static_cast<std::size_t>(instruction.operand);
    };
    switch (instruction.opcode) {
    case OpCode::Nop: case OpCode::Suspend: break;
    case OpCode::PushConst:
        if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) >= function.constants.size())
            throw std::runtime_error("constant index out of range");
        Push(function.constants[static_cast<std::size_t>(instruction.operand)]); break;
    case OpCode::PushVoid: Push(Value{}); break;
    case OpCode::LoadLocal:
        if (slot() >= locals_.size()) throw std::runtime_error("local slot out of range");
        Push(locals_[slot()]); break;
    case OpCode::StoreLocal: {
        const auto index = slot();
        if (index >= locals_.size()) throw std::runtime_error("local slot out of range");
        locals_[index] = Pop(); break;
    }
    case OpCode::Dup: { Value value = Pop(); Push(value); Push(std::move(value)); break; }
    case OpCode::Pop: Pop(); break;
    case OpCode::ToFloat: Push(Value(AsFloat(Pop()))); break;
    case OpCode::ToString: Push(Value(Pop().ToString())); break;
    case OpCode::AddInt: case OpCode::SubInt: case OpCode::MulInt: case OpCode::DivInt: case OpCode::ModInt:
    case OpCode::AddFloat: case OpCode::SubFloat: case OpCode::MulFloat: case OpCode::DivFloat:
        BinaryArithmetic(instruction); break;
    case OpCode::Concat: { Value right = Pop(), left = Pop(); Push(Value(left.As<std::string>() + right.As<std::string>())); break; }
    case OpCode::NegInt: Push(Value(-Pop().As<std::int32_t>())); break;
    case OpCode::NegFloat: Push(Value(-Pop().As<float>())); break;
    case OpCode::LogicalNot: Push(Value(!Pop().As<bool>())); break;
    case OpCode::Equal: case OpCode::NotEqual: case OpCode::Less: case OpCode::LessEqual:
    case OpCode::Greater: case OpCode::GreaterEqual: Compare(instruction); break;
    case OpCode::Return:
        result_.returnValue = Pop(); result_.state = ExecutionState::Finished; break;
    case OpCode::Jump: case OpCode::JumpIfFalse: case OpCode::Call: case OpCode::CallHost:
        throw std::runtime_error("opcode is not executable in this VM stage");
    }
    return result_.state == ExecutionState::Active;
}

Value VirtualMachine::Pop() {
    if (stack_.empty()) throw std::runtime_error("operand stack underflow");
    Value value = std::move(stack_.back());
    stack_.pop_back();
    return value;
}
void VirtualMachine::Push(Value value) { stack_.push_back(std::move(value)); }

void VirtualMachine::Fail(const Instruction& instruction, std::string message) {
    result_.state = ExecutionState::Exception;
    result_.exception = std::move(message);
    result_.location = instruction.location;
}

void VirtualMachine::BinaryArithmetic(const Instruction& instruction) {
    Value right = Pop(), left = Pop();
    if (instruction.opcode >= OpCode::AddFloat && instruction.opcode <= OpCode::DivFloat) {
        const float a = AsFloat(left), b = AsFloat(right);
        if (instruction.opcode == OpCode::DivFloat && b == 0.0f) throw std::runtime_error("division by zero");
        switch (instruction.opcode) {
        case OpCode::AddFloat: Push(Value(a + b)); break;
        case OpCode::SubFloat: Push(Value(a - b)); break;
        case OpCode::MulFloat: Push(Value(a * b)); break;
        case OpCode::DivFloat: Push(Value(a / b)); break;
        default: break;
        }
        return;
    }
    const auto a = left.As<std::int32_t>(), b = right.As<std::int32_t>();
    if ((instruction.opcode == OpCode::DivInt || instruction.opcode == OpCode::ModInt) && b == 0)
        throw std::runtime_error("division by zero");
    switch (instruction.opcode) {
    case OpCode::AddInt: Push(Value(a + b)); break;
    case OpCode::SubInt: Push(Value(a - b)); break;
    case OpCode::MulInt: Push(Value(a * b)); break;
    case OpCode::DivInt: Push(Value(a / b)); break;
    case OpCode::ModInt: Push(Value(a % b)); break;
    default: break;
    }
}

void VirtualMachine::Compare(const Instruction& instruction) {
    Value right = Pop(), left = Pop();
    bool result = false;
    if (instruction.opcode == OpCode::Equal || instruction.opcode == OpCode::NotEqual) {
        result = ValuesEqual(left, right);
        if (instruction.opcode == OpCode::NotEqual) result = !result;
    } else {
        const float a = AsFloat(left), b = AsFloat(right);
        if (instruction.opcode == OpCode::Less) result = a < b;
        else if (instruction.opcode == OpCode::LessEqual) result = a <= b;
        else if (instruction.opcode == OpCode::Greater) result = a > b;
        else result = a >= b;
    }
    Push(Value(result));
}

} // namespace mini_as

