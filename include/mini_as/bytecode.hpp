#pragma once

#include "mini_as/type_checker.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace mini_as {

enum class OpCode : std::uint8_t {
    Nop, Suspend,
    PushConst, PushVoid, LoadLocal, StoreLocal, Dup, Pop,
    ToFloat, ToString,
    AddInt, SubInt, MulInt, DivInt, ModInt,
    AddFloat, SubFloat, MulFloat, DivFloat,
    Concat, NegInt, NegFloat, LogicalNot,
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    Jump, JumpIfFalse,
    Call, CallHost, Return
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

struct BytecodeModule {
    std::vector<BytecodeFunction> functions;
};

std::string_view OpCodeName(OpCode opcode);
std::string Disassemble(const BytecodeFunction& function);

class BytecodeCompiler {
public:
    explicit BytecodeCompiler(DiagnosticSink& diagnostics);
    BytecodeModule Compile(AstNode* root, const std::vector<FunctionSignature>& signatures);

private:
    void CompileFunction(AstNode* node, const FunctionSignature& signature);
    void CompileBlock(AstNode* node, bool createScope = true);
    void CompileStatement(AstNode* node);
    void CompileExpression(AstNode* node);
    void CompileBinary(AstNode* node);
    void Emit(OpCode opcode, std::int32_t operand, const AstNode* node);
    std::int32_t AddConstant(Value value);
    std::optional<std::int32_t> LookupLocal(std::string_view name) const;
    std::int32_t DeclareLocal(const Token& name);
    void Error(const AstNode* node, std::string message);

    DiagnosticSink& diagnostics_;
    BytecodeModule module_;
    BytecodeFunction* function_ = nullptr;
    std::vector<std::unordered_map<std::string, std::int32_t>> scopes_;
    std::int32_t nextLocal_ = 0;
};

} // namespace mini_as

