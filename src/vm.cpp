#include "mini_as/vm.hpp"
#include "mini_as/generic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mini_as {
namespace {

float AsFloat(const Value& value) {
    if (value.Type() == DataType::Float()) return value.As<float>();
    if (value.Type() == DataType::Double()) return static_cast<float>(value.As<double>());
    if (value.Type().IsSignedInteger()) return static_cast<float>(value.SignedInteger());
    if (value.Type().IsUnsignedInteger()) return static_cast<float>(value.UnsignedInteger());
    throw std::runtime_error("expected numeric value");
}

double AsDouble(const Value& value) {
    if (value.Type() == DataType::Double()) return value.As<double>();
    if (value.Type() == DataType::Float()) return value.As<float>();
    if (value.Type().IsSignedInteger()) return static_cast<double>(value.SignedInteger());
    if (value.Type().IsUnsignedInteger()) return static_cast<double>(value.UnsignedInteger());
    throw std::runtime_error("expected numeric value");
}

bool ValuesEqual(const Value& left, const Value& right) {
    if (left.Type().kind == TypeKind::Function && right.Type().kind == TypeKind::Object &&
        right.Type().objectName == "<null>") return !left.As<FunctionHandle>();
    if (right.Type().kind == TypeKind::Function && left.Type().kind == TypeKind::Object &&
        left.Type().objectName == "<null>") return !right.As<FunctionHandle>();
    if ((left.Type().kind == TypeKind::WeakRef || left.Type().kind == TypeKind::ConstWeakRef) &&
        right.Type().kind == TypeKind::Object)
        return left.As<WeakObjectHandle>().Equals(right.As<ObjectHandle>());
    if ((right.Type().kind == TypeKind::WeakRef || right.Type().kind == TypeKind::ConstWeakRef) &&
        left.Type().kind == TypeKind::Object)
        return right.As<WeakObjectHandle>().Equals(left.As<ObjectHandle>());
    if ((left.Type().kind == TypeKind::WeakRef || left.Type().kind == TypeKind::ConstWeakRef) &&
        (right.Type().kind == TypeKind::WeakRef || right.Type().kind == TypeKind::ConstWeakRef)) {
        const auto& leftWeak = left.As<WeakObjectHandle>();
        const auto& rightWeak = right.As<WeakObjectHandle>();
        return leftWeak.SameTarget(rightWeak);
    }
    if (left.Type().IsInteger() && right.Type().IsInteger()) {
        if (left.Type().IsSignedInteger() && right.Type().IsSignedInteger())
            return left.SignedInteger() == right.SignedInteger();
        if (left.Type().IsUnsignedInteger() && right.Type().IsUnsignedInteger())
            return left.UnsignedInteger() == right.UnsignedInteger();
        if (left.Type().IsSignedInteger())
            return left.SignedInteger() >= 0 &&
                   static_cast<std::uint64_t>(left.SignedInteger()) == right.UnsignedInteger();
        return right.SignedInteger() >= 0 &&
               left.UnsignedInteger() == static_cast<std::uint64_t>(right.SignedInteger());
    }
    if (left.Type().IsNumeric() && right.Type().IsNumeric()) return AsDouble(left) == AsDouble(right);
    return left == right;
}

ParameterMode ParameterModeAt(const FunctionSignature& signature, std::size_t index) {
    const std::size_t fixedCount = signature.variadic && !signature.parameters.empty()
        ? signature.parameters.size() - 1 : signature.parameters.size();
    const std::size_t prototype = signature.variadic && index >= fixedCount
        ? signature.parameters.size() - 1 : index;
    return prototype < signature.parameterModes.size()
        ? signature.parameterModes[prototype] : ParameterMode::Value;
}

const DataType& ParameterTypeAt(const FunctionSignature& signature, std::size_t index) {
    const std::size_t fixedCount = signature.variadic && !signature.parameters.empty()
        ? signature.parameters.size() - 1 : signature.parameters.size();
    if (signature.variadic && index >= fixedCount) return signature.parameters.back();
    return signature.parameters.at(index);
}

bool MatchesDeclaredType(const Value& value, const DataType& expected) {
    if (expected.kind == TypeKind::Var) return true;
    if (value.Type() == expected) return true;
    if (value.Type().kind != TypeKind::Object || expected.kind != TypeKind::Object) return false;
    const auto& handle = value.As<ObjectHandle>();
    if (!handle) return expected.isHandle;
    const auto* object = dynamic_cast<const ScriptObject*>(handle.Get());
    return object && (object->Implements(expected.objectName) || object->IsA(expected.objectName));
}

Value LoadObjectField(const ObjectHandle& handle, std::size_t index) {
    if (!handle) throw std::runtime_error("null object field access");
    if (auto* object = dynamic_cast<ScriptObject*>(handle.Get())) {
        if (index >= object->FieldCount()) throw std::runtime_error("field index out of range");
        return object->GetField(index);
    }
    const TypeInfo* type = handle.Get()->GetTypeInfo();
    if (!type || index >= type->hostProperties.size() || !type->hostProperties[index] ||
        !type->hostProperties[index]->getter)
        throw std::runtime_error("registered object property is unavailable");
    const auto* property = type->hostProperties[index];
    Value value;
    try { value = property->getter(handle); }
    catch (const std::exception& error) {
        throw std::runtime_error(std::string("host property exception: ") + error.what());
    }
    if (!MatchesDeclaredType(value, property->signature.type))
        throw std::runtime_error("registered object property getter returned " +
                                 value.Type().Name() + " but declared " +
                                 property->signature.type.Name());
    return value;
}

void StoreObjectField(const ObjectHandle& handle, std::size_t index, Value value) {
    if (!handle) throw std::runtime_error("null object field assignment");
    if (auto* object = dynamic_cast<ScriptObject*>(handle.Get())) {
        if (index >= object->FieldCount()) throw std::runtime_error("field index out of range");
        object->SetField(index, std::move(value));
        return;
    }
    const TypeInfo* type = handle.Get()->GetTypeInfo();
    if (!type || index >= type->hostProperties.size() || !type->hostProperties[index])
        throw std::runtime_error("registered object property is unavailable");
    const auto* property = type->hostProperties[index];
    if (property->signature.isConst || !property->setter)
        throw std::runtime_error("registered object property is read-only");
    if (!MatchesDeclaredType(value, property->signature.type))
        throw std::runtime_error("registered object property setter received " +
                                 value.Type().Name() + " but declared " +
                                 property->signature.type.Name());
    try { property->setter(handle, std::move(value)); }
    catch (const std::exception& error) {
        throw std::runtime_error(std::string("host property exception: ") + error.what());
    }
}

bool CheckedMultiply(std::int64_t left, std::int64_t right, std::int64_t& result) {
    if (left == 0 || right == 0) { result = 0; return true; }
    if ((left == -1 && right == std::numeric_limits<std::int64_t>::min()) ||
        (right == -1 && left == std::numeric_limits<std::int64_t>::min())) return false;
    if (left > 0) {
        if (right > 0 && left > std::numeric_limits<std::int64_t>::max() / right) return false;
        if (right < 0 && right < std::numeric_limits<std::int64_t>::min() / left) return false;
    } else {
        if (right > 0 && left < std::numeric_limits<std::int64_t>::min() / right) return false;
        if (right < 0 && left < std::numeric_limits<std::int64_t>::max() / right) return false;
    }
    result = left * right;
    return true;
}

std::int64_t SignedPower(std::int64_t base, std::int64_t exponent) {
    if (exponent < 0) {
        if (base == 0) throw std::runtime_error("exponent overflow");
        return 0;
    }
    if (base == 0 && exponent == 0) throw std::runtime_error("exponent overflow");
    std::int64_t result = 1;
    while (exponent) {
        if (exponent & 1) {
            if (!CheckedMultiply(result, base, result)) throw std::runtime_error("exponent overflow");
        }
        exponent >>= 1;
        if (exponent && !CheckedMultiply(base, base, base))
            throw std::runtime_error("exponent overflow");
    }
    return result;
}

std::uint64_t UnsignedPower(std::uint64_t base, std::uint64_t exponent) {
    if (base == 0 && exponent == 0) throw std::runtime_error("exponent overflow");
    std::uint64_t result = 1;
    while (exponent) {
        if (exponent & 1) {
            if (base && result > std::numeric_limits<std::uint64_t>::max() / base)
                throw std::runtime_error("exponent overflow");
            result *= base;
        }
        exponent >>= 1;
        if (exponent) {
            if (base && base > std::numeric_limits<std::uint64_t>::max() / base)
                throw std::runtime_error("exponent overflow");
            base *= base;
        }
    }
    return result;
}

Value LoadGlobalValue(const BytecodeModule& module, ModuleState& state, GlobalId id) {
    const auto index = module.FindGlobalIndex(id);
    if (!index || *index >= state.globals.size())
        throw std::runtime_error("global slot is unavailable");
    const auto& binding = module.globals[*index];
    if (!binding.host) return state.globals[*index];
    if (!binding.host->storage)
        throw std::runtime_error("registered global property storage is unavailable");
    if (binding.host->storage->Type() != binding.signature.type)
        throw std::runtime_error("registered global property type changed");
    return *binding.host->storage;
}

void StoreGlobalValue(const BytecodeModule& module, ModuleState& state, GlobalId id, Value value) {
    const auto index = module.FindGlobalIndex(id);
    if (!index || *index >= state.globals.size())
        throw std::runtime_error("global slot is unavailable");
    const auto& binding = module.globals[*index];
    if (binding.signature.isConst && binding.host)
        throw std::runtime_error("cannot write const global property");
    if (!binding.host) {
        state.globals[*index] = std::move(value);
        return;
    }
    if (!binding.host->storage)
        throw std::runtime_error("registered global property storage is unavailable");
    if (binding.host->storage->Type() != binding.signature.type ||
        value.Type() != binding.signature.type)
        throw std::runtime_error("registered global property type changed");
    *binding.host->storage = std::move(value);
}

} // namespace

bool VirtualMachine::Prepare(const BytecodeFunction& function, const std::vector<Value>& arguments,
                             const BytecodeModule* module, ModuleState* state) {
    stack_.clear();
    callStack_.clear();
    captures_.clear();
    locals_.assign(function.localCount, Value{});
    function_ = &function;
    module_ = module;
    moduleState_ = state;
    pc_ = 0;
    stackBase_ = 0;
    suspendRequested_ = false;
    result_ = {};
    result_.state = ExecutionState::Prepared;
    const std::size_t hiddenArguments = function.signature.method ? 1 : 0;
    if (arguments.size() != function.signature.parameters.size() + hiddenArguments) {
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
            HandleException(function_->code[pc_ ? pc_ - 1 : 0], error.what());
        }
        if (safePoint_) safePoint_();
    }
    if (result_.state == ExecutionState::Exception || result_.state == ExecutionState::Aborted) {
        stack_.clear();
        locals_.clear();
        captures_.clear();
        callStack_.clear();
        if (safePoint_) safePoint_();
    }
    return result_;
}

void VirtualMachine::RequestSuspend() { suspendRequested_ = true; }
void VirtualMachine::Abort() {
    result_.state = ExecutionState::Aborted;
    stack_.clear();
    locals_.clear();
    captures_.clear();
    callStack_.clear();
    if (safePoint_) safePoint_();
}
void VirtualMachine::SetLineCallback(std::function<void(const SourceLocation&)> callback) {
    lineCallback_ = std::move(callback);
}

void VirtualMachine::SetFinalizerContext(ObjectFinalizerQueue* queue,
                                         std::shared_ptr<const BytecodeModule> module,
                                         std::weak_ptr<ModuleState> state,
                                         std::function<void()> safePoint) {
    finalizerQueue_ = queue;
    finalizerModule_ = std::move(module);
    finalizerState_ = std::move(state);
    safePoint_ = std::move(safePoint);
}

void VirtualMachine::SetModuleOwner(std::shared_ptr<const void> owner) {
    moduleOwner_ = std::move(owner);
}

void VirtualMachine::SetScriptFunctionResolver(
    std::function<std::optional<ResolvedScriptFunction>(FunctionId)> resolver) {
    scriptFunctionResolver_ = std::move(resolver);
}

std::size_t VirtualMachine::GetCallStackSize() const {
    if (result_.state == ExecutionState::Exception) return result_.callStack.size();
    if (!function_ || (result_.state != ExecutionState::Prepared &&
                       result_.state != ExecutionState::Active &&
                       result_.state != ExecutionState::Suspended)) return 0;
    return callStack_.size() + 1;
}

const BytecodeFunction* VirtualMachine::GetFunction(std::size_t stackLevel) const {
    if (result_.state == ExecutionState::Exception)
        return stackLevel < result_.callStack.size()
            ? result_.callStack[stackLevel].function : nullptr;
    if (!function_ || (result_.state != ExecutionState::Prepared &&
                       result_.state != ExecutionState::Active &&
                       result_.state != ExecutionState::Suspended) ||
        stackLevel > callStack_.size()) return nullptr;
    if (stackLevel == 0) return function_;
    return callStack_[callStack_.size() - stackLevel].function;
}

SourceLocation VirtualMachine::GetInstructionLocation(std::size_t stackLevel) const {
    if (result_.state == ExecutionState::Exception)
        return stackLevel < result_.callStack.size()
            ? result_.callStack[stackLevel].location : SourceLocation{};
    if (!function_ || (result_.state != ExecutionState::Prepared &&
                       result_.state != ExecutionState::Active &&
                       result_.state != ExecutionState::Suspended) ||
        stackLevel > callStack_.size()) return {};
    if (stackLevel == 0) return MakeStackFrame(function_, pc_, locals_).location;
    const auto& frame = callStack_[callStack_.size() - stackLevel];
    return MakeStackFrame(frame.function, frame.pc, frame.locals).location;
}

std::vector<LocalVariableInfo> VirtualMachine::GetLocals(std::size_t stackLevel) const {
    if (result_.state == ExecutionState::Exception)
        return stackLevel < result_.callStack.size()
            ? result_.callStack[stackLevel].locals : std::vector<LocalVariableInfo>{};
    if (!function_ || (result_.state != ExecutionState::Prepared &&
                       result_.state != ExecutionState::Active &&
                       result_.state != ExecutionState::Suspended) ||
        stackLevel > callStack_.size()) return {};
    if (stackLevel == 0) return MakeStackFrame(function_, pc_, locals_).locals;
    const auto& frame = callStack_[callStack_.size() - stackLevel];
    return MakeStackFrame(frame.function, frame.pc, frame.locals).locals;
}

ExecutionResult VirtualMachine::Execute(const BytecodeFunction& function,
                                        const std::vector<Value>& arguments,
                                        const BytecodeModule* module, ModuleState* state) {
    if (!Prepare(function, arguments, module, state)) return result_;
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
        if (const auto* cell = std::get_if<CapturedCellHandle>(&locals_[slot()].Raw())) {
            if (!*cell) throw std::runtime_error("captured local cell is unavailable");
            Push((*cell)->value);
        } else Push(locals_[slot()]);
        break;
    case OpCode::StoreLocal: {
        const auto index = slot();
        if (index >= locals_.size()) throw std::runtime_error("local slot out of range");
        Value stored = Pop();
        if (const auto* cell = std::get_if<CapturedCellHandle>(&locals_[index].Raw())) {
            if (!*cell) throw std::runtime_error("captured local cell is unavailable");
            (*cell)->value = std::move(stored);
        } else locals_[index] = std::move(stored);
        break;
    }
    case OpCode::CaptureLocal: {
        const auto index = slot();
        if (index >= locals_.size()) throw std::runtime_error("captured local slot out of range");
        CapturedCellHandle cell;
        if (const auto* existing = std::get_if<CapturedCellHandle>(&locals_[index].Raw()))
            cell = *existing;
        else {
            cell = std::make_shared<CapturedCell>();
            cell->value = std::move(locals_[index]);
            locals_[index] = Value(cell);
        }
        Push(Value(std::move(cell)));
        break;
    }
    case OpCode::CaptureCapture: {
        const auto index = slot();
        if (index >= captures_.size()) throw std::runtime_error("parent capture slot out of range");
        Push(Value(captures_[index]));
        break;
    }
    case OpCode::LoadCapture: {
        const auto index = slot();
        if (index >= captures_.size() || !captures_[index])
            throw std::runtime_error("capture slot is unavailable");
        Push(captures_[index]->value);
        break;
    }
    case OpCode::StoreCapture: {
        const auto index = slot();
        if (index >= captures_.size() || !captures_[index])
            throw std::runtime_error("capture slot is unavailable");
        captures_[index]->value = Pop();
        break;
    }
    case OpCode::LoadGlobal: {
        if (!module_ || !moduleState_ || instruction.operand < 0)
            throw std::runtime_error("module global state is unavailable");
        Push(LoadGlobalValue(*module_, *moduleState_,
                             GlobalId{static_cast<std::uint32_t>(instruction.operand)}));
        break;
    }
    case OpCode::StoreGlobal: {
        if (!module_ || !moduleState_ || instruction.operand < 0)
            throw std::runtime_error("module global state is unavailable");
        StoreGlobalValue(*module_, *moduleState_,
                         GlobalId{static_cast<std::uint32_t>(instruction.operand)}, Pop());
        break;
    }
    case OpCode::Dup: { Value value = Pop(); Push(value); Push(std::move(value)); break; }
    case OpCode::Swap: {
        Value top = Pop(), below = Pop();
        Push(std::move(top));
        Push(std::move(below));
        break;
    }
    case OpCode::Pop: Pop(); break;
    case OpCode::ToFloat: Push(Value(AsFloat(Pop()))); break;
    case OpCode::ToDouble: Push(Value(AsDouble(Pop()))); break;
    case OpCode::ToInteger: {
        const DataType target{static_cast<TypeKind>(instruction.operand), {}, false};
        if (!target.IsInteger()) throw std::runtime_error("invalid integer conversion target");
        Push(ConvertInteger(Pop(), target));
        break;
    }
    case OpCode::ToString: Push(Value(Pop().ToString())); break;
    case OpCode::AddInt: case OpCode::SubInt: case OpCode::MulInt: case OpCode::DivInt:
    case OpCode::ModInt: case OpCode::PowInt:
    case OpCode::BitAnd: case OpCode::BitOr: case OpCode::BitXor:
    case OpCode::ShiftLeft: case OpCode::ShiftRight: case OpCode::ShiftRightArithmetic:
    case OpCode::AddFloat: case OpCode::SubFloat: case OpCode::MulFloat:
    case OpCode::DivFloat: case OpCode::PowFloat:
    case OpCode::AddDouble: case OpCode::SubDouble: case OpCode::MulDouble:
    case OpCode::DivDouble: case OpCode::PowDouble:
        BinaryArithmetic(instruction); break;
    case OpCode::Concat: { Value right = Pop(), left = Pop(); Push(Value(left.As<std::string>() + right.As<std::string>())); break; }
    case OpCode::NegInt: {
        Value operand = Pop();
        Push(Value::Integer(operand.Type(), std::uint64_t{0} - operand.UnsignedInteger()));
        break;
    }
    case OpCode::NegFloat: Push(Value(-Pop().As<float>())); break;
    case OpCode::NegDouble: Push(Value(-Pop().As<double>())); break;
    case OpCode::BitNot: {
        Value operand = Pop();
        Push(Value::Integer(operand.Type(), ~operand.UnsignedInteger()));
        break;
    }
    case OpCode::LogicalNot: Push(Value(!Pop().As<bool>())); break;
    case OpCode::Equal: case OpCode::NotEqual: case OpCode::Less: case OpCode::LessEqual:
    case OpCode::Greater: case OpCode::GreaterEqual: Compare(instruction); break;
    case OpCode::Return: {
        Value returnValue = Pop();
        std::vector<Value> outputArguments;
        const std::size_t parameterOffset = function_->signature.method ? 1 : 0;
        for (std::size_t index = 0; index < function_->signature.parameters.size(); ++index) {
            const ParameterMode mode = ParameterModeAt(function_->signature, index);
            if (mode == ParameterMode::Out || mode == ParameterMode::InOut)
                outputArguments.push_back(locals_.at(parameterOffset + index));
        }
        if (callStack_.empty()) {
            result_.returnValue = std::move(returnValue);
            result_.state = ExecutionState::Finished;
            stack_.clear();
            locals_.clear();
            captures_.clear();
        } else {
            CallFrame frame = std::move(callStack_.back());
            callStack_.pop_back();
            function_ = frame.function;
            pc_ = frame.pc;
            locals_ = std::move(frame.locals);
            stackBase_ = frame.stackBase;
            captures_ = std::move(frame.captures);
            module_ = frame.module;
            moduleState_ = frame.state;
            moduleOwner_ = std::move(frame.owner);
            finalizerModule_ = std::move(frame.finalizerModule);
            finalizerState_ = std::move(frame.finalizerState);
            Push(std::move(returnValue));
            for (auto& output : outputArguments) Push(std::move(output));
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
                          callable->kind != CallableKind::ScriptMethod &&
                          callable->kind != CallableKind::ImportedFunction &&
                          callable->kind != CallableKind::ExternalFunction))
            throw std::runtime_error("call descriptor kind does not match opcode");
        const BytecodeFunction* target = nullptr;
        std::optional<ResolvedScriptFunction> resolved;
        if (callable->kind == CallableKind::ImportedFunction) {
            if (!moduleState_) throw std::runtime_error("imported function state is unavailable");
            const FunctionId bound = moduleState_->FindImportedFunction(callable->function);
            if (!bound.IsValid()) throw std::runtime_error("imported function is not bound");
            if (const auto* hostTarget = module_->FindHostFunction(bound)) {
                std::vector<Value> hostArguments(callable->parameterCount);
                for (std::size_t i = hostArguments.size(); i > 0; --i)
                    hostArguments[i - 1] = Pop();
                std::vector<DataType> hostArgumentTypes;
                hostArgumentTypes.reserve(hostArguments.size());
                for (const auto& argument : hostArguments)
                    hostArgumentTypes.push_back(argument.Type());
                GenericCall call(hostArguments, {}, callable->argumentTypes);
                try { hostTarget->callback(call); }
                catch (const std::exception& error) {
                    throw std::runtime_error(std::string("host exception: ") + error.what());
                }
                if (!call.Exception().empty()) throw std::runtime_error(call.Exception());
                if (call.ReturnValue().Type() != hostTarget->signature.returnType)
                    throw std::runtime_error("host function returned " +
                        call.ReturnValue().Type().Name() + " but declared " +
                        hostTarget->signature.returnType.Name());
                Push(call.ReturnValue());
                for (std::size_t index = 0; index < hostArguments.size(); ++index) {
                    const ParameterMode mode = ParameterModeAt(hostTarget->signature, index);
                    if (mode == ParameterMode::Out || mode == ParameterMode::InOut) {
                        const DataType& declared = ParameterTypeAt(hostTarget->signature, index);
                        const DataType& expected = declared.kind == TypeKind::Var &&
                            index < callable->argumentTypes.size()
                                ? callable->argumentTypes[index]
                                : (declared.kind == TypeKind::Var
                                    ? hostArgumentTypes[index] : declared);
                        if (!MatchesDeclaredType(hostArguments[index], expected))
                            throw std::runtime_error("host function wrote an incompatible output value");
                        Push(std::move(hostArguments[index]));
                    }
                }
                break;
            }
            if (!scriptFunctionResolver_)
                throw std::runtime_error("imported function resolver is unavailable");
            resolved = scriptFunctionResolver_(bound);
            if (!resolved || !resolved->function || !resolved->module || !resolved->state)
                throw std::runtime_error("bound imported function is unavailable");
            target = resolved->function;
        } else if (callable->kind == CallableKind::ExternalFunction) {
            if (!scriptFunctionResolver_)
                throw std::runtime_error("external shared function resolver is unavailable");
            resolved = scriptFunctionResolver_(callable->function);
            if (!resolved || !resolved->function || !resolved->module || !resolved->state)
                throw std::runtime_error("external shared function is unavailable");
            target = resolved->function;
        } else target = module_->FindFunction(callable->function);
        if (!target) throw std::runtime_error("call target is unavailable");
        const std::size_t hiddenArguments = target->signature.method ? 1 : 0;
        std::vector<Value> arguments(target->signature.parameters.size() + hiddenArguments);
        for (std::size_t i = arguments.size(); i > 0; --i) arguments[i - 1] = Pop();
        callStack_.push_back({function_, pc_, std::move(locals_), stackBase_, std::move(captures_),
                              module_, moduleState_, moduleOwner_, finalizerModule_, finalizerState_});
        if (resolved) {
            module_ = resolved->module;
            moduleState_ = resolved->state;
            moduleOwner_ = std::move(resolved->owner);
            finalizerModule_ = std::move(resolved->finalizerModule);
            finalizerState_ = std::move(resolved->finalizerState);
        }
        function_ = target;
        pc_ = 0;
        stackBase_ = stack_.size();
        locals_.assign(target->localCount, Value{});
        captures_.clear();
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
        std::vector<Value> arguments(callable->parameterCount);
        for (std::size_t i = arguments.size(); i > 0; --i) arguments[i - 1] = Pop();
        std::vector<DataType> argumentTypes;
        argumentTypes.reserve(arguments.size());
        for (const auto& argument : arguments) argumentTypes.push_back(argument.Type());
        Value receiverValue;
        if (callable->kind == CallableKind::HostMethod) {
            receiverValue = Pop();
            const TypeInfo* expected = module_->FindType(callable->objectType);
            if (!expected) throw std::runtime_error("host method receiver type mismatch");
            if (expected->valueType) {
                if (receiverValue.Type() != DataType::Object(expected->name, false))
                    throw std::runtime_error("host method receiver type mismatch");
            } else {
                const auto& receiver = receiverValue.As<ObjectHandle>();
                if (!receiver || !receiver.Get()->GetTypeInfo())
                    throw std::runtime_error("null host method receiver");
                if (!receiver.Get()->GetTypeInfo()->IsA(expected->name))
                    throw std::runtime_error("host method receiver type mismatch");
            }
        }
        GenericCall call(arguments, std::move(receiverValue), callable->argumentTypes);
        try { target->callback(call); }
        catch (const std::exception& error) { throw std::runtime_error(std::string("host exception: ") + error.what()); }
        if (!call.Exception().empty()) throw std::runtime_error(call.Exception());
        if (target->signature.factory) {
            if (call.ReturnValue().Type().kind == TypeKind::Object &&
                !call.ReturnValue().As<ObjectHandle>())
                throw std::runtime_error("object factory returned null without an exception");
            if (!MatchesDeclaredType(call.ReturnValue(), target->signature.returnType))
                throw std::runtime_error("object factory returned " +
                                         call.ReturnValue().Type().Name() +
                                         " but declared " +
                                         target->signature.returnType.Name());
        } else if (call.ReturnValue().Type() != target->signature.returnType) {
            throw std::runtime_error("host function returned " + call.ReturnValue().Type().Name() +
                                     " but declared " + target->signature.returnType.Name());
        }
        Push(call.ReturnValue());
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const ParameterMode mode = ParameterModeAt(target->signature, index);
            if (mode == ParameterMode::Out || mode == ParameterMode::InOut) {
                const DataType& declared = ParameterTypeAt(target->signature, index);
                const DataType& expected = declared.kind == TypeKind::Var &&
                    index < callable->argumentTypes.size()
                        ? callable->argumentTypes[index]
                        : (declared.kind == TypeKind::Var ? argumentTypes[index] : declared);
                if (!MatchesDeclaredType(arguments[index], expected))
                    throw std::runtime_error("host function wrote " + arguments[index].Type().Name() +
                                             " to " + expected.Name() +
                                             " output parameter");
                Push(std::move(arguments[index]));
            }
        }
        break;
    }
    case OpCode::CallVirtual: {
        if (!module_ || instruction.operand < 0) throw std::runtime_error("virtual call target is unavailable");
        if (callStack_.size() >= 1024) throw std::runtime_error("script call stack overflow");
        const CallableRef* callable = module_->FindCallable(static_cast<std::size_t>(instruction.operand));
        if (!callable || callable->kind != CallableKind::VirtualMethod)
            throw std::runtime_error("call descriptor kind does not match opcode");
        std::vector<Value> arguments(static_cast<std::size_t>(callable->parameterCount) + 1);
        for (std::size_t i = arguments.size(); i > 0; --i) arguments[i - 1] = Pop();
        const auto& receiver = arguments[0].As<ObjectHandle>();
        if (!receiver || !receiver.Get()->GetTypeInfo()) throw std::runtime_error("null virtual method receiver");
        const BytecodeFunction* target = module_->ResolveVirtual(
            receiver.Get()->GetTypeInfo()->id, callable->objectType, callable->virtualSlot);
        std::optional<ResolvedScriptFunction> resolved;
        if (!target && scriptFunctionResolver_) {
            const FunctionId targetId = module_->ResolveVirtualFunctionId(
                receiver.Get()->GetTypeInfo()->id, callable->objectType, callable->virtualSlot);
            if (targetId.IsValid()) {
                resolved = scriptFunctionResolver_(targetId);
                if (resolved) target = resolved->function;
            }
        }
        if (!target) throw std::runtime_error("virtual method implementation is unavailable");
        callStack_.push_back({function_, pc_, std::move(locals_), stackBase_, std::move(captures_),
                              module_, moduleState_, moduleOwner_, finalizerModule_, finalizerState_});
        if (resolved) {
            module_ = resolved->module;
            moduleState_ = resolved->state;
            moduleOwner_ = std::move(resolved->owner);
            finalizerModule_ = std::move(resolved->finalizerModule);
            finalizerState_ = std::move(resolved->finalizerState);
        }
        function_ = target;
        pc_ = 0;
        stackBase_ = stack_.size();
        locals_.assign(target->localCount, Value{});
        captures_.clear();
        for (std::size_t i = 0; i < arguments.size(); ++i) locals_[i] = std::move(arguments[i]);
        break;
    }
    case OpCode::CallHandle: {
        if (!module_ || instruction.operand < 0)
            throw std::runtime_error("function handle call target is unavailable");
        const CallableRef* callable = module_->FindCallable(static_cast<std::size_t>(instruction.operand));
        if (!callable || callable->kind != CallableKind::FunctionHandle)
            throw std::runtime_error("call descriptor kind does not match opcode");
        std::vector<Value> arguments(static_cast<std::size_t>(callable->parameterCount));
        for (std::size_t i = arguments.size(); i > 0; --i) arguments[i - 1] = Pop();
        const FunctionHandle handle = Pop().As<FunctionHandle>();
        if (!handle) throw std::runtime_error("null function handle invocation");
        if (handle.signature != callable->objectType)
            throw std::runtime_error("function handle signature mismatch");
        if (handle.host) {
            const auto* target = module_->FindHostFunction(handle.function);
            if (!target) throw std::runtime_error("host function handle target is unavailable");
            std::vector<DataType> argumentTypes;
            argumentTypes.reserve(arguments.size());
            for (const auto& argument : arguments) argumentTypes.push_back(argument.Type());
            GenericCall call(arguments, {}, callable->argumentTypes);
            try { target->callback(call); }
            catch (const std::exception& error) {
                throw std::runtime_error(std::string("host exception: ") + error.what());
            }
            if (!call.Exception().empty()) throw std::runtime_error(call.Exception());
            if (call.ReturnValue().Type() != target->signature.returnType) {
                throw std::runtime_error("host function returned " + call.ReturnValue().Type().Name() +
                                         " but declared " + target->signature.returnType.Name());
            }
            Push(call.ReturnValue());
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                const ParameterMode mode = ParameterModeAt(target->signature, index);
                if (mode == ParameterMode::Out || mode == ParameterMode::InOut) {
                    const DataType& declared = ParameterTypeAt(target->signature, index);
                    const DataType& expected = declared.kind == TypeKind::Var &&
                        index < callable->argumentTypes.size()
                            ? callable->argumentTypes[index]
                            : (declared.kind == TypeKind::Var
                                ? argumentTypes[index] : declared);
                    if (!MatchesDeclaredType(arguments[index], expected))
                        throw std::runtime_error("host function handle wrote an incompatible output value");
                    Push(std::move(arguments[index]));
                }
            }
            break;
        }
        if (callStack_.size() >= 1024) throw std::runtime_error("script call stack overflow");
        const BytecodeFunction* target = nullptr;
        std::optional<ResolvedScriptFunction> resolved;
        if (handle.virtualMethod) {
            if (!handle.object || !handle.object.Get()->GetTypeInfo())
                throw std::runtime_error("null delegate object");
            target = module_->ResolveVirtual(handle.object.Get()->GetTypeInfo()->id,
                                             handle.dispatchType, handle.virtualSlot);
            if (!target && scriptFunctionResolver_) {
                const FunctionId targetId = module_->ResolveVirtualFunctionId(
                    handle.object.Get()->GetTypeInfo()->id, handle.dispatchType,
                    handle.virtualSlot);
                if (targetId.IsValid()) {
                    resolved = scriptFunctionResolver_(targetId);
                    if (resolved) target = resolved->function;
                }
            }
        } else {
            target = module_->FindFunction(handle.function);
            if (!target && scriptFunctionResolver_) {
                resolved = scriptFunctionResolver_(handle.function);
                if (resolved) target = resolved->function;
            }
        }
        if (!target) throw std::runtime_error("script function handle target is unavailable");
        if (target->signature.parameters.size() != arguments.size())
            throw std::runtime_error("function handle argument count mismatch");
        if (handle.virtualMethod) arguments.insert(arguments.begin(), Value(handle.object));
        callStack_.push_back({function_, pc_, std::move(locals_), stackBase_, std::move(captures_),
                              module_, moduleState_, moduleOwner_, finalizerModule_, finalizerState_});
        if (resolved) {
            module_ = resolved->module;
            moduleState_ = resolved->state;
            moduleOwner_ = std::move(resolved->owner);
            finalizerModule_ = std::move(resolved->finalizerModule);
            finalizerState_ = std::move(resolved->finalizerState);
        }
        function_ = target;
        pc_ = 0;
        stackBase_ = stack_.size();
        locals_.assign(target->localCount, Value{});
        captures_ = handle.captures;
        for (std::size_t i = 0; i < arguments.size(); ++i) locals_[i] = std::move(arguments[i]);
        break;
    }
    case OpCode::MakeDelegate: {
        if (!module_ || instruction.operand < 0)
            throw std::runtime_error("delegate target is unavailable");
        const CallableRef* callable = module_->FindCallable(static_cast<std::size_t>(instruction.operand));
        if (!callable || callable->kind != CallableKind::VirtualMethod)
            throw std::runtime_error("delegate descriptor kind does not match opcode");
        const FuncdefSignature* funcdef = module_->FindFuncdef(callable->signatureType);
        if (!funcdef) throw std::runtime_error("delegate funcdef is unavailable");
        Value receiverValue = Pop();
        const ObjectHandle receiver = receiverValue.As<ObjectHandle>();
        if (!receiver) throw std::runtime_error("cannot create delegate with null object");
        Push(Value(FunctionHandle{{}, funcdef->id, funcdef->name, false, receiver,
                                  callable->objectType, callable->virtualSlot, true}));
        break;
    }
    case OpCode::MakeClosure: {
        if (!module_ || instruction.operand < 0)
            throw std::runtime_error("anonymous function target is unavailable");
        const CallableRef* callable = module_->FindCallable(static_cast<std::size_t>(instruction.operand));
        if (!callable || callable->kind != CallableKind::ScriptFunction)
            throw std::runtime_error("closure descriptor kind does not match opcode");
        const FuncdefSignature* funcdef = module_->FindFuncdef(callable->signatureType);
        if (!funcdef) throw std::runtime_error("anonymous function funcdef is unavailable");
        std::vector<CapturedCellHandle> captures(static_cast<std::size_t>(callable->parameterCount));
        for (std::size_t index = captures.size(); index > 0; --index)
            captures[index - 1] = Pop().As<CapturedCellHandle>();
        Push(Value(FunctionHandle{callable->function, funcdef->id, funcdef->name, false,
                                  {}, {}, 0, false, std::move(captures)}));
        break;
    }
    case OpCode::CastObject: {
        if (!module_ || instruction.operand < 0)
            throw std::runtime_error("reference cast target is unavailable");
        const TypeInfo* target = module_->FindType(
            TypeId{static_cast<std::uint32_t>(instruction.operand)});
        if (!target) throw std::runtime_error("reference cast target is unavailable");
        Value source = Pop();
        const auto& handle = source.As<ObjectHandle>();
        if (!handle) {
            Push(Value(ObjectHandle{}));
            break;
        }
        const auto* object = dynamic_cast<const ScriptObject*>(handle.Get());
        if (!object) throw std::runtime_error("reference cast source is not a script object");
        if (object->IsA(target->name) || object->Implements(target->name)) Push(std::move(source));
        else Push(Value(ObjectHandle{}));
        break;
    }
    case OpCode::NewObject: {
        if (!module_ || instruction.operand < 0) throw std::runtime_error("object type is unavailable");
        const TypeInfo* type = module_->FindType(TypeId{static_cast<std::uint32_t>(instruction.operand)});
        if (!type) throw std::runtime_error("object type is unavailable");
        ScriptFinalizerBinding finalizer;
        if (finalizerQueue_ && finalizerModule_) {
            finalizer.functions = module_->FindDestructors(type->id);
            finalizer.module = finalizerModule_;
            finalizer.state = finalizerState_;
        }
        Push(Value(ObjectHandle(new ScriptObject(type, finalizerQueue_, std::move(finalizer)))));
        break;
    }
    case OpCode::CopyObject: {
        if (!module_ || instruction.operand < 0)
            throw std::runtime_error("copy constructor type is unavailable");
        const TypeInfo* type = module_->FindType(TypeId{static_cast<std::uint32_t>(instruction.operand)});
        if (!type) throw std::runtime_error("copy constructor type is unavailable");
        Value sourceValue = Pop();
        Value destinationValue = Pop();
        const auto& sourceHandle = sourceValue.As<ObjectHandle>();
        const auto& destinationHandle = destinationValue.As<ObjectHandle>();
        auto* source = sourceHandle ? dynamic_cast<ScriptObject*>(sourceHandle.Get()) : nullptr;
        auto* destination = destinationHandle
            ? dynamic_cast<ScriptObject*>(destinationHandle.Get()) : nullptr;
        if (!source || !destination)
            throw std::runtime_error("copy constructor source is null or not a script object");
        if (destination->GetTypeInfo() != type || !destination->CopyFieldsFrom(*source))
            throw std::runtime_error("copy constructor object types are incompatible");
        Push(std::move(destinationValue));
        break;
    }
    case OpCode::MakeWeakRef: {
        if (instruction.operand < 0 ||
            static_cast<std::size_t>(instruction.operand) >= function_->constants.size())
            throw std::runtime_error("weakref type descriptor is unavailable");
        const auto& descriptor = function_->constants[static_cast<std::size_t>(instruction.operand)]
            .As<WeakObjectHandle>();
        const ObjectHandle object = Pop().As<ObjectHandle>();
        Push(Value(WeakObjectHandle(object, descriptor.TypeName(), descriptor.IsReadOnly())));
        break;
    }
    case OpCode::LockWeakRef: {
        const WeakObjectHandle weak = Pop().As<WeakObjectHandle>();
        Push(Value(weak.Lock()));
        break;
    }
    case OpCode::ToConstWeakRef:
        Push(Value(Pop().As<WeakObjectHandle>().AsReadOnly()));
        break;
    case OpCode::LoadField: {
        Value objectValue = Pop();
        const auto& handle = objectValue.As<ObjectHandle>();
        if (instruction.operand < 0) throw std::runtime_error("field index out of range");
        Push(LoadObjectField(handle, static_cast<std::size_t>(instruction.operand)));
        break;
    }
    case OpCode::StoreField: {
        Value fieldValue = Pop();
        Value objectValue = Pop();
        const auto& handle = objectValue.As<ObjectHandle>();
        if (instruction.operand < 0) throw std::runtime_error("field index out of range");
        StoreObjectField(handle, static_cast<std::size_t>(instruction.operand), fieldValue);
        Push(std::move(fieldValue));
        break;
    }
    case OpCode::MakeGlobalReference: {
        if (!module_ || !moduleState_ || instruction.operand < 0)
            throw std::runtime_error("global reference target is unavailable");
        const GlobalId id{static_cast<std::uint32_t>(instruction.operand)};
        const auto index = module_->FindGlobalIndex(id);
        if (!index) throw std::runtime_error("global reference target is unavailable");
        Push(Value(ReferenceStorage{ReferenceKind::Global,
            module_->globals[*index].signature.type, id.value, {}}));
        break;
    }
    case OpCode::MakeFieldReference: {
        Value objectValue = Pop();
        const auto& handle = objectValue.As<ObjectHandle>();
        if (!handle)
            throw std::runtime_error("null or non-script object reference target");
        if (!handle.Get()->GetTypeInfo())
            throw std::runtime_error("object reference target type is unavailable");
        if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) >=
            handle.Get()->GetTypeInfo()->fields.size())
            throw std::runtime_error("field reference index out of range");
        const auto field = static_cast<std::uint32_t>(instruction.operand);
        Push(Value(ReferenceStorage{ReferenceKind::Field,
            handle.Get()->GetTypeInfo()->fields[field].second, field, handle}));
        break;
    }
    case OpCode::LoadReference: {
        const auto reference = Pop().As<ReferenceStorage>();
        if (reference.kind == ReferenceKind::Global) {
            if (!module_ || !moduleState_) throw std::runtime_error("global reference is unavailable");
            Push(LoadGlobalValue(*module_, *moduleState_, GlobalId{reference.slot}));
        } else {
            Push(LoadObjectField(reference.object, reference.slot));
        }
        break;
    }
    case OpCode::StoreReference: {
        Value stored = Pop();
        const auto reference = Pop().As<ReferenceStorage>();
        if (reference.kind == ReferenceKind::Global) {
            if (!module_ || !moduleState_) throw std::runtime_error("global reference is unavailable");
            StoreGlobalValue(*module_, *moduleState_, GlobalId{reference.slot}, stored);
        } else {
            StoreObjectField(reference.object, reference.slot, stored);
        }
        Push(std::move(stored));
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
    if (function_) result_.callStack.push_back(MakeStackFrame(function_, pc_, locals_));
    for (auto frame = callStack_.rbegin(); frame != callStack_.rend(); ++frame) {
        result_.callStack.push_back(MakeStackFrame(frame->function, frame->pc, frame->locals));
    }
}

StackFrameInfo VirtualMachine::MakeStackFrame(const BytecodeFunction* function, std::size_t pc,
                                              const std::vector<Value>& locals) const {
    StackFrameInfo result;
    result.function = function;
    result.functionDeclaration = function ? function->signature.Declaration() : "<unknown>";
    if (!function || function->code.empty()) return result;
    result.instructionOffset = pc ? std::min(pc - 1, function->code.size() - 1) : 0;
    result.location = function->code[result.instructionOffset].location;
    result.locals.reserve(function->debugVariables.size());
    for (const auto& variable : function->debugVariables) {
        LocalVariableInfo local;
        local.name = variable.name;
        local.type = variable.type;
        local.slot = variable.slot;
        local.isConst = variable.isConst;
        local.parameter = variable.parameter;
        local.inScope = variable.scopeBegin <= result.instructionOffset &&
                        result.instructionOffset < variable.scopeEnd;
        if (variable.slot.value < locals.size()) {
            local.value = locals[variable.slot.value];
            if (const auto* cell = std::get_if<CapturedCellHandle>(&local.value.Raw()))
                local.value = *cell ? (*cell)->value : Value{};
        }
        result.locals.push_back(std::move(local));
    }
    return result;
}

bool VirtualMachine::HandleException(const Instruction& instruction, std::string message) {
    const auto findHandler = [](const BytecodeFunction* function, std::size_t faultPc)
        -> const ExceptionHandler* {
        if (!function) return nullptr;
        const ExceptionHandler* selected = nullptr;
        for (const auto& handler : function->exceptionHandlers) {
            if (faultPc < handler.tryBegin || faultPc >= handler.tryEnd) continue;
            if (!selected || handler.tryEnd - handler.tryBegin <
                             selected->tryEnd - selected->tryBegin) selected = &handler;
        }
        return selected;
    };

    const std::size_t currentFault = pc_ ? pc_ - 1 : 0;
    const ExceptionHandler* handler = findHandler(function_, currentFault);
    std::size_t unwindCount = 0;
    if (!handler) {
        std::size_t depth = 0;
        for (auto frame = callStack_.rbegin(); frame != callStack_.rend(); ++frame) {
            ++depth;
            const std::size_t faultPc = frame->pc ? frame->pc - 1 : 0;
            handler = findHandler(frame->function, faultPc);
            if (handler) { unwindCount = depth; break; }
        }
    }
    if (!handler) {
        Fail(instruction, std::move(message));
        return false;
    }

    while (unwindCount-- > 0) {
        stack_.resize(stackBase_);
        CallFrame frame = std::move(callStack_.back());
        callStack_.pop_back();
        function_ = frame.function;
        pc_ = frame.pc;
        locals_ = std::move(frame.locals);
        stackBase_ = frame.stackBase;
        module_ = frame.module;
        moduleState_ = frame.state;
        moduleOwner_ = std::move(frame.owner);
        finalizerModule_ = std::move(frame.finalizerModule);
        finalizerState_ = std::move(frame.finalizerState);
        captures_ = std::move(frame.captures);
    }
    stack_.resize(stackBase_);
    pc_ = handler->catchTarget;
    result_.state = ExecutionState::Active;
    result_.exception.clear();
    result_.location = {};
    result_.callStack.clear();
    return true;
}

void VirtualMachine::BinaryArithmetic(const Instruction& instruction) {
    Value right = Pop(), left = Pop();
    if (instruction.opcode >= OpCode::AddDouble && instruction.opcode <= OpCode::PowDouble) {
        const double a = AsDouble(left), b = AsDouble(right);
        if (instruction.opcode == OpCode::DivDouble && b == 0.0)
            throw std::runtime_error("division by zero");
        switch (instruction.opcode) {
        case OpCode::AddDouble: Push(Value(a + b)); break;
        case OpCode::SubDouble: Push(Value(a - b)); break;
        case OpCode::MulDouble: Push(Value(a * b)); break;
        case OpCode::DivDouble: Push(Value(a / b)); break;
        case OpCode::PowDouble: Push(Value(std::pow(a, b))); break;
        default: break;
        }
        return;
    }
    if (instruction.opcode >= OpCode::AddFloat && instruction.opcode <= OpCode::PowFloat) {
        const float a = AsFloat(left), b = AsFloat(right);
        if (instruction.opcode == OpCode::DivFloat && b == 0.0f) throw std::runtime_error("division by zero");
        switch (instruction.opcode) {
        case OpCode::AddFloat: Push(Value(a + b)); break;
        case OpCode::SubFloat: Push(Value(a - b)); break;
        case OpCode::MulFloat: Push(Value(a * b)); break;
        case OpCode::DivFloat: Push(Value(a / b)); break;
        case OpCode::PowFloat: Push(Value(std::pow(a, b))); break;
        default: break;
        }
        return;
    }
    if (!left.Type().IsInteger() || left.Type() != right.Type())
        throw std::runtime_error("integer operands have incompatible types");
    const DataType type = left.Type();
    const std::uint64_t a = left.UnsignedInteger(), b = right.UnsignedInteger();
    if ((instruction.opcode == OpCode::DivInt || instruction.opcode == OpCode::ModInt) && b == 0)
        throw std::runtime_error("division by zero");
    switch (instruction.opcode) {
    case OpCode::AddInt: Push(Value::Integer(type, a + b)); break;
    case OpCode::SubInt: Push(Value::Integer(type, a - b)); break;
    case OpCode::MulInt: Push(Value::Integer(type, a * b)); break;
    case OpCode::DivInt:
        if (type.IsSignedInteger()) {
            const auto signedA = left.SignedInteger(), signedB = right.SignedInteger();
            if (signedA == std::numeric_limits<std::int64_t>::min() && signedB == -1)
                Push(Value::Integer(type, static_cast<std::uint64_t>(signedA)));
            else Push(Value::Integer(type, static_cast<std::uint64_t>(signedA / signedB)));
        } else Push(Value::Integer(type, a / b));
        break;
    case OpCode::ModInt:
        if (type.IsSignedInteger()) {
            const auto signedA = left.SignedInteger(), signedB = right.SignedInteger();
            if (signedA == std::numeric_limits<std::int64_t>::min() && signedB == -1)
                Push(Value::Integer(type, 0));
            else Push(Value::Integer(type, static_cast<std::uint64_t>(signedA % signedB)));
        } else Push(Value::Integer(type, a % b));
        break;
    case OpCode::PowInt:
        if (type.IsSignedInteger()) {
            const auto powered = SignedPower(left.SignedInteger(), right.SignedInteger());
            if (type.IntegerBits() < 64) {
                const auto minimum = -(std::int64_t{1} << (type.IntegerBits() - 1));
                const auto maximum = (std::int64_t{1} << (type.IntegerBits() - 1)) - 1;
                if (powered < minimum || powered > maximum)
                    throw std::runtime_error("exponent overflow");
            }
            Push(Value::Integer(type, static_cast<std::uint64_t>(powered)));
        } else {
            const auto powered = UnsignedPower(a, b);
            if (type.IntegerBits() < 64 && powered >= (std::uint64_t{1} << type.IntegerBits()))
                throw std::runtime_error("exponent overflow");
            Push(Value::Integer(type, powered));
        }
        break;
    case OpCode::BitAnd: Push(Value::Integer(type, a & b)); break;
    case OpCode::BitOr: Push(Value::Integer(type, a | b)); break;
    case OpCode::BitXor: Push(Value::Integer(type, a ^ b)); break;
    case OpCode::ShiftLeft: {
        const auto count = static_cast<unsigned>(b & (type.IntegerBits() - 1));
        Push(Value::Integer(type, a << count));
        break;
    }
    case OpCode::ShiftRight: {
        const auto count = static_cast<unsigned>(b & (type.IntegerBits() - 1));
        Push(Value::Integer(type, a >> count));
        break;
    }
    case OpCode::ShiftRightArithmetic: {
        const auto count = static_cast<unsigned>(b & (type.IntegerBits() - 1));
        if (type.IsSignedInteger())
            Push(Value::Integer(type, static_cast<std::uint64_t>(left.SignedInteger() >> count)));
        else Push(Value::Integer(type, a >> count));
        break;
    }
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
        if (left.Type().IsInteger() && right.Type().IsInteger() && left.Type() == right.Type()) {
            if (left.Type().IsSignedInteger()) {
                const auto a = left.SignedInteger(), b = right.SignedInteger();
                if (instruction.opcode == OpCode::Less) result = a < b;
                else if (instruction.opcode == OpCode::LessEqual) result = a <= b;
                else if (instruction.opcode == OpCode::Greater) result = a > b;
                else result = a >= b;
            } else {
                const auto a = left.UnsignedInteger(), b = right.UnsignedInteger();
                if (instruction.opcode == OpCode::Less) result = a < b;
                else if (instruction.opcode == OpCode::LessEqual) result = a <= b;
                else if (instruction.opcode == OpCode::Greater) result = a > b;
                else result = a >= b;
            }
        } else {
            const double a = AsDouble(left), b = AsDouble(right);
            if (instruction.opcode == OpCode::Less) result = a < b;
            else if (instruction.opcode == OpCode::LessEqual) result = a <= b;
            else if (instruction.opcode == OpCode::Greater) result = a > b;
            else result = a >= b;
        }
    }
    Push(Value(result));
}

} // namespace mini_as
