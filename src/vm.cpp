#include "mini_as/vm.hpp"
#include "mini_as/generic.hpp"

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

bool VirtualMachine::Prepare(const BytecodeFunction& function, const std::vector<Value>& arguments,
                             const BytecodeModule* module) {
    stack_.clear();
    callStack_.clear();
    locals_.assign(function.localCount, Value{});
    function_ = &function;
    module_ = module;
    pc_ = 0;
    suspendRequested_ = false;
    result_ = {};
    result_.state = ExecutionState::Prepared;
    if (arguments.size() != function.signature.parameters.size()) {
        result_.state = ExecutionState::Exception;
        result_.exception = "argument count does not match function signature";
        return false;
    }
    for (std::size_t i = 0; i < arguments.size(); ++i) locals_[i] = arguments[i];
    return true;
}

ExecutionResult VirtualMachine::Continue() {
    if (!function_ || (result_.state != ExecutionState::Prepared && result_.state != ExecutionState::Suspended)) {
        result_.state = ExecutionState::Exception;
        result_.exception = "VM is not prepared or suspended";
        return result_;
    }
    result_.state = ExecutionState::Active;
    while (result_.state == ExecutionState::Active) {
        if (pc_ >= function_->code.size()) {
            result_.state = ExecutionState::Exception;
            result_.exception = "program counter escaped function";
            break;
        }
        try { Step(); }
        catch (const std::exception& error) {
            Fail(function_->code[pc_ ? pc_ - 1 : 0], error.what());
        }
    }
    return result_;
}

void VirtualMachine::RequestSuspend() { suspendRequested_ = true; }
void VirtualMachine::Abort() { result_.state = ExecutionState::Aborted; }
void VirtualMachine::SetLineCallback(std::function<void(const SourceLocation&)> callback) {
    lineCallback_ = std::move(callback);
}

ExecutionResult VirtualMachine::Execute(const BytecodeFunction& function,
                                        const std::vector<Value>& arguments,
                                        const BytecodeModule* module) {
    if (!Prepare(function, arguments, module)) return result_;
    return Continue();
}

bool VirtualMachine::Step() {
    const BytecodeFunction& function = *function_;
    const Instruction instruction = function.code[pc_++];
    const auto slot = [&]() -> std::size_t {
        if (instruction.operand < 0) throw std::runtime_error("negative local slot");
        return static_cast<std::size_t>(instruction.operand);
    };
    switch (instruction.opcode) {
    case OpCode::Nop: break;
    case OpCode::Suspend:
        if (lineCallback_) lineCallback_(instruction.location);
        if (result_.state == ExecutionState::Aborted) break;
        if (suspendRequested_) { suspendRequested_ = false; result_.state = ExecutionState::Suspended; }
        break;
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
    case OpCode::Return: {
        Value returnValue = Pop();
        if (callStack_.empty()) {
            result_.returnValue = std::move(returnValue);
            result_.state = ExecutionState::Finished;
        } else {
            CallFrame frame = std::move(callStack_.back());
            callStack_.pop_back();
            function_ = frame.function;
            pc_ = frame.pc;
            locals_ = std::move(frame.locals);
            Push(std::move(returnValue));
        }
        break;
    }
    case OpCode::Jump:
        if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) > function.code.size())
            throw std::runtime_error("jump target out of range");
        pc_ = static_cast<std::size_t>(instruction.operand); break;
    case OpCode::JumpIfFalse: {
        const bool condition = Pop().As<bool>();
        if (!condition) {
            if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) > function.code.size())
                throw std::runtime_error("jump target out of range");
            pc_ = static_cast<std::size_t>(instruction.operand);
        }
        break;
    }
    case OpCode::Call: {
        if (!module_ || instruction.operand < 0) throw std::runtime_error("call target is unavailable");
        if (callStack_.size() >= 1024) throw std::runtime_error("script call stack overflow");
        const CallableRef* callable = module_->FindCallable(static_cast<std::size_t>(instruction.operand));
        if (!callable || (callable->kind != CallableKind::ScriptFunction &&
                          callable->kind != CallableKind::ScriptMethod))
            throw std::runtime_error("call descriptor kind does not match opcode");
        const BytecodeFunction* target = module_->FindFunction(callable->function);
        if (!target) throw std::runtime_error("call target is unavailable");
        std::vector<Value> arguments(target->signature.parameters.size());
        for (std::size_t i = arguments.size(); i > 0; --i) arguments[i - 1] = Pop();
        callStack_.push_back({function_, pc_, std::move(locals_)});
        function_ = target;
        pc_ = 0;
        locals_.assign(target->localCount, Value{});
        for (std::size_t i = 0; i < arguments.size(); ++i) locals_[i] = std::move(arguments[i]);
        break;
    }
    case OpCode::CallHost: {
        if (!module_ || instruction.operand < 0) throw std::runtime_error("host call target is unavailable");
        const CallableRef* callable = module_->FindCallable(static_cast<std::size_t>(instruction.operand));
        if (!callable || (callable->kind != CallableKind::HostFunction &&
                          callable->kind != CallableKind::HostMethod))
            throw std::runtime_error("call descriptor kind does not match opcode");
        const auto* target = module_->FindHostFunction(callable->function);
        if (!target) throw std::runtime_error("host call target is unavailable");
        std::vector<Value> arguments(target->signature.parameters.size());
        for (std::size_t i = arguments.size(); i > 0; --i) arguments[i - 1] = Pop();
        GenericCall call(arguments);
        try { target->callback(call); }
        catch (const std::exception& error) { throw std::runtime_error(std::string("host exception: ") + error.what()); }
        if (!call.Exception().empty()) throw std::runtime_error(call.Exception());
        if (call.ReturnValue().Type() != target->signature.returnType) {
            throw std::runtime_error("host function returned " + call.ReturnValue().Type().Name() +
                                     " but declared " + target->signature.returnType.Name());
        }
        Push(call.ReturnValue());
        break;
    }
    case OpCode::NewObject: {
        if (!module_ || instruction.operand < 0) throw std::runtime_error("object type is unavailable");
        const TypeInfo* type = module_->FindType(TypeId{static_cast<std::uint32_t>(instruction.operand)});
        if (!type) throw std::runtime_error("object type is unavailable");
        Push(Value(ObjectHandle(new ScriptObject(type))));
        break;
    }
    case OpCode::LoadField: {
        Value objectValue = Pop();
        const auto& handle = objectValue.As<ObjectHandle>();
        auto* object = handle ? dynamic_cast<ScriptObject*>(handle.Get()) : nullptr;
        if (!object) throw std::runtime_error("null or non-script object field access");
        if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) >= object->FieldCount())
            throw std::runtime_error("field index out of range");
        Push(object->GetField(static_cast<std::size_t>(instruction.operand)));
        break;
    }
    case OpCode::StoreField: {
        Value fieldValue = Pop();
        Value objectValue = Pop();
        const auto& handle = objectValue.As<ObjectHandle>();
        auto* object = handle ? dynamic_cast<ScriptObject*>(handle.Get()) : nullptr;
        if (!object) throw std::runtime_error("null or non-script object field assignment");
        if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) >= object->FieldCount())
            throw std::runtime_error("field index out of range");
        object->SetField(static_cast<std::size_t>(instruction.operand), fieldValue);
        Push(std::move(fieldValue));
        break;
    }
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
    result_.callStack.clear();
    if (function_) result_.callStack.push_back({function_->signature.Declaration(), instruction.location});
    for (auto frame = callStack_.rbegin(); frame != callStack_.rend(); ++frame) {
        SourceLocation location;
        if (frame->function && frame->pc && frame->pc - 1 < frame->function->code.size())
            location = frame->function->code[frame->pc - 1].location;
        result_.callStack.push_back({frame->function ? frame->function->signature.Declaration() : "<unknown>",
                                     std::move(location)});
    }
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
