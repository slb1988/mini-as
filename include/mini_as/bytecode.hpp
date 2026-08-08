#pragma once

#include "mini_as/type_checker.hpp"
#include "mini_as/object.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace mini_as {

struct RegisteredHostFunction;

enum class OpCode : std::uint8_t {
    Nop, Suspend,
    PushConst, PushVoid, LoadLocal, StoreLocal, LoadGlobal, StoreGlobal, Dup, Swap, Pop,
    ToFloat, ToString,
    AddInt, SubInt, MulInt, DivInt, ModInt,
    AddFloat, SubFloat, MulFloat, DivFloat,
    Concat, NegInt, NegFloat, LogicalNot,
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    Jump, JumpIfFalse,
    Call, CallHost, NewObject, LoadField, StoreField, Return
};

struct Instruction {
    OpCode opcode = OpCode::Nop;
    std::int32_t operand = 0;
    SourceLocation location;
};

struct BytecodeFunction {
    FunctionSignature signature;
    std::vector<Instruction> code;
    std::vector<Value> constants;
    std::size_t localCount = 0;
};

enum class CallableKind {
    ScriptFunction,
    HostFunction,
    ScriptMethod,
    HostMethod,
    VirtualMethod
};

struct CallableRef {
    CallableKind kind = CallableKind::ScriptFunction;
    FunctionId function;
    TypeId objectType;
    std::uint32_t virtualSlot = 0;
};

struct GlobalBinding {
    GlobalSignature signature;
};

struct ModuleState {
    std::vector<Value> globals;
};

struct BytecodeModule {
    std::vector<BytecodeFunction> functions;
    BytecodeFunction globalInitializer;
    std::vector<GlobalBinding> globals;
    std::vector<CallableRef> callables;
    std::vector<std::pair<FunctionId, const RegisteredHostFunction*>> hostFunctions;
    std::vector<std::pair<TypeId, const TypeInfo*>> objectTypes;

    const BytecodeFunction* FindFunction(FunctionId id) const;
    const RegisteredHostFunction* FindHostFunction(FunctionId id) const;
    const TypeInfo* FindType(TypeId id) const;
    const CallableRef* FindCallable(std::size_t index) const;
    std::optional<std::size_t> FindGlobalIndex(GlobalId id) const;
};

std::string_view OpCodeName(OpCode opcode);
std::string Disassemble(const BytecodeFunction& function);

class BytecodeCompiler {
public:
    explicit BytecodeCompiler(DiagnosticSink& diagnostics);
    BytecodeModule Compile(AstNode* root, const std::vector<FunctionSignature>& signatures,
                           const std::vector<ClassSignature>& classes = {},
                           const std::vector<GlobalSignature>& globals = {});

private:
    struct LValueRef {
        enum class Kind { Local, Global, Field, Index } kind = Kind::Local;
        DataType type = DataType::Invalid();
        VariableId variable;
        GlobalId global;
        std::uint32_t field = 0;
        AstNode* receiver = nullptr;
    };

    struct ControlFlowContext {
        std::vector<std::size_t> breakJumps;
        std::vector<std::size_t> continueJumps;
        bool loop = false;
        std::size_t continueTarget = 0;
    };

    void CompileFunction(AstNode* node, std::size_t functionIndex);
    void CompileGlobalInitializer(AstNode* root);
    void CompileBlock(AstNode* node, bool createScope = true);
    void CompileStatement(AstNode* node);
    void CompileExpression(AstNode* node);
    void CompileBinary(AstNode* node);
    void CompileLogical(AstNode* node);
    void CompileCall(AstNode* node);
    std::optional<LValueRef> ResolveLValue(AstNode* expression) const;
    void CompileLValueLoad(const LValueRef& target, const AstNode* source);
    void CompileLValueStore(const LValueRef& target, AstNode* value, const AstNode* source);
    void CompileCompoundAssignment(const LValueRef& target, AstNode* value,
                                   TokenKind operation, const AstNode* source);
    void CompileIncrement(AstNode* node);
    std::int32_t AddCallable(CallableRef callable);
    std::optional<std::pair<std::size_t, DataType>> FindField(const AstNode* member) const;
    std::size_t Emit(OpCode opcode, std::int32_t operand, const AstNode* node);
    void PatchJump(std::size_t instruction, std::size_t target);
    std::int32_t AddConstant(Value value);
    std::optional<VariableId> LookupLocal(std::string_view name) const;
    VariableId DeclareLocal(const Token& name);
    void Error(const AstNode* node, std::string message);

    DiagnosticSink& diagnostics_;
    BytecodeModule module_;
    BytecodeFunction* function_ = nullptr;
    std::vector<std::unordered_map<std::string, VariableId>> scopes_;
    std::uint32_t nextLocal_ = 0;
    std::vector<FunctionSignature> signatures_;
    std::unordered_map<std::string, std::size_t> functionIndices_;
    std::unordered_map<std::string, FunctionId> functionIds_;
    std::unordered_map<std::string, FunctionId> hostIds_;
    std::vector<ClassSignature> classes_;
    std::unordered_map<std::string, TypeId> classIds_;
    std::vector<GlobalSignature> globals_;
    std::unordered_map<std::string, GlobalSignature> globalSymbols_;
    std::vector<ControlFlowContext> controlFlow_;
};

} // namespace mini_as
