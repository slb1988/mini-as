#include "mini_as/vm.hpp"
#include "mini_as/generic.hpp"

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
    return index < signature.parameterModes.size()
        ? signature.parameterModes[index] : ParameterMode::Value;
}

bool MatchesDeclaredType(const Value& value, const DataType& expected) {
    if (value.Type() == expected) return true;
    if (value.Type().kind != TypeKind::Object || expected.kind != TypeKind::Object) return false;
    const auto& handle = value.As<ObjectHandle>();
    if (!handle) return expected.isHandle;
    const auto* object = dynamic_cast<const ScriptObject*>(handle.Get());
    return object && (object->Implements(expected.objectName) || object->IsA(expected.objectName));
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

} // namespace

bool VirtualMachine::Prepare(const BytecodeFunction& function, const std::vector<Value>& arguments,
                             const BytecodeModule* module, ModuleState* state) {
    stack_.clear();
    callStack_.clear();
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
        Push(locals_[slot()]); break;
    case OpCode::StoreLocal: {
        const auto index = slot();
        if (index >= locals_.size()) throw std::runtime_error("local slot out of range");
        locals_[index] = Pop(); break;
    }
    case OpCode::LoadGlobal: {
        if (!module_ || !moduleState_ || instruction.operand < 0)
            throw std::runtime_error("module global state is unavailable");
        const auto index = module_->FindGlobalIndex(GlobalId{static_cast<std::uint32_t>(instruction.operand)});
        if (!index || *index >= moduleState_->globals.size())
            throw std::runtime_error("global slot is unavailable");
        Push(moduleState_->globals[*index]);
        break;
    }
    case OpCode::StoreGlobal: {
        if (!module_ || !moduleState_ || instruction.operand < 0)
            throw std::runtime_error("module global state is unavailable");
        const auto index = module_->FindGlobalIndex(GlobalId{static_cast<std::uint32_t>(instruction.operand)});
        if (!index || *index >= moduleState_->globals.size())
            throw std::runtime_error("global slot is unavailable");
        moduleState_->globals[*index] = Pop();
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
        } else {
            CallFrame frame = std::move(callStack_.back());
            callStack_.pop_back();
            function_ = frame.function;
            pc_ = frame.pc;
            locals_ = std::move(frame.locals);
            stackBase_ = frame.stackBase;
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
                          callable->kind != CallableKind::ScriptMethod))
            throw std::runtime_error("call descriptor kind does not match opcode");
        const BytecodeFunction* target = module_->FindFunction(callable->function);
        if (!target) throw std::runtime_error("call target is unavailable");
        const std::size_t hiddenArguments = callable->kind == CallableKind::ScriptMethod ? 1 : 0;
        std::vector<Value> arguments(target->signature.parameters.size() + hiddenArguments);
        for (std::size_t i = arguments.size(); i > 0; --i) arguments[i - 1] = Pop();
        callStack_.push_back({function_, pc_, std::move(locals_), stackBase_});
        function_ = target;
        pc_ = 0;
        stackBase_ = stack_.size();
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
        for (std::size_t index = 0; index < target->signature.parameters.size(); ++index) {
            const ParameterMode mode = ParameterModeAt(target->signature, index);
            if (mode == ParameterMode::Out || mode == ParameterMode::InOut) {
                if (!MatchesDeclaredType(arguments[index], target->signature.parameters[index]))
                    throw std::runtime_error("host function wrote " + arguments[index].Type().Name() +
                                             " to " + target->signature.parameters[index].Name() +
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
        if (!target) throw std::runtime_error("virtual method implementation is unavailable");
        callStack_.push_back({function_, pc_, std::move(locals_), stackBase_});
        function_ = target;
        pc_ = 0;
        stackBase_ = stack_.size();
        locals_.assign(target->localCount, Value{});
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
            GenericCall call(arguments);
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
            for (std::size_t index = 0; index < target->signature.parameters.size(); ++index) {
                const ParameterMode mode = ParameterModeAt(target->signature, index);
                if (mode == ParameterMode::Out || mode == ParameterMode::InOut) {
                    if (!MatchesDeclaredType(arguments[index], target->signature.parameters[index]))
                        throw std::runtime_error("host function handle wrote an incompatible output value");
                    Push(std::move(arguments[index]));
                }
            }
            break;
        }
        if (callStack_.size() >= 1024) throw std::runtime_error("script call stack overflow");
        const BytecodeFunction* target = module_->FindFunction(handle.function);
        if (!target) throw std::runtime_error("script function handle target is unavailable");
        if (target->signature.parameters.size() != arguments.size())
            throw std::runtime_error("function handle argument count mismatch");
        callStack_.push_back({function_, pc_, std::move(locals_), stackBase_});
        function_ = target;
        pc_ = 0;
        stackBase_ = stack_.size();
        locals_.assign(target->localCount, Value{});
        for (std::size_t i = 0; i < arguments.size(); ++i) locals_[i] = std::move(arguments[i]);
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
        auto* object = handle ? dynamic_cast<ScriptObject*>(handle.Get()) : nullptr;
        if (!object) throw std::runtime_error("null or non-script object reference target");
        if (instruction.operand < 0 ||
            static_cast<std::size_t>(instruction.operand) >= object->FieldCount())
            throw std::runtime_error("field reference index out of range");
        const auto field = static_cast<std::uint32_t>(instruction.operand);
        Push(Value(ReferenceStorage{ReferenceKind::Field,
            object->GetTypeInfo()->fields[field].second, field, handle}));
        break;
    }
    case OpCode::LoadReference: {
        const auto reference = Pop().As<ReferenceStorage>();
        if (reference.kind == ReferenceKind::Global) {
            if (!module_ || !moduleState_) throw std::runtime_error("global reference is unavailable");
            const auto index = module_->FindGlobalIndex(GlobalId{reference.slot});
            if (!index) throw std::runtime_error("global reference is unavailable");
            Push(moduleState_->globals.at(*index));
        } else {
            auto* object = reference.object ? dynamic_cast<ScriptObject*>(reference.object.Get()) : nullptr;
            if (!object) throw std::runtime_error("field reference is unavailable");
            Push(object->GetField(reference.slot));
        }
        break;
    }
    case OpCode::StoreReference: {
        Value stored = Pop();
        const auto reference = Pop().As<ReferenceStorage>();
        if (reference.kind == ReferenceKind::Global) {
            if (!module_ || !moduleState_) throw std::runtime_error("global reference is unavailable");
            const auto index = module_->FindGlobalIndex(GlobalId{reference.slot});
            if (!index) throw std::runtime_error("global reference is unavailable");
            moduleState_->globals.at(*index) = stored;
        } else {
            auto* object = reference.object ? dynamic_cast<ScriptObject*>(reference.object.Get()) : nullptr;
            if (!object) throw std::runtime_error("field reference is unavailable");
            object->SetField(reference.slot, stored);
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
    if (function_) result_.callStack.push_back({function_->signature.Declaration(), instruction.location});
    for (auto frame = callStack_.rbegin(); frame != callStack_.rend(); ++frame) {
        SourceLocation location;
        if (frame->function && frame->pc && frame->pc - 1 < frame->function->code.size())
            location = frame->function->code[frame->pc - 1].location;
        result_.callStack.push_back({frame->function ? frame->function->signature.Declaration() : "<unknown>",
                                     std::move(location)});
    }
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
