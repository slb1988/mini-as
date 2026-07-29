#include "mini_as/bytecode.hpp"

#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace mini_as {
namespace {

std::string DecodeString(std::string_view text) {
    std::string result;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        char ch = text[i];
        if (ch == '\\' && i + 1 < text.size() - 1) {
            ch = text[++i];
            if (ch == 'n') result += '\n';
            else if (ch == 't') result += '\t';
            else if (ch == 'r') result += '\r';
            else result += ch;
        } else result += ch;
    }
    return result;
}

} // namespace

std::string_view OpCodeName(OpCode opcode) {
    static const char* names[] = {
        "NOP", "SUSPEND", "PUSH_CONST", "PUSH_VOID", "LOAD_LOCAL", "STORE_LOCAL", "DUP", "POP",
        "TO_FLOAT", "TO_STRING", "ADD_I", "SUB_I", "MUL_I", "DIV_I", "MOD_I",
        "ADD_F", "SUB_F", "MUL_F", "DIV_F", "CONCAT", "NEG_I", "NEG_F", "NOT",
        "EQ", "NE", "LT", "LE", "GT", "GE", "JMP", "JZ", "CALL", "CALL_HOST", "RET"
    };
    return names[static_cast<std::size_t>(opcode)];
}

std::string Disassemble(const BytecodeFunction& function) {
    std::ostringstream out;
    out << function.signature.Declaration() << "\n";
    for (std::size_t i = 0; i < function.code.size(); ++i) {
        const auto& instruction = function.code[i];
        out << std::setw(4) << i << "  " << std::left << std::setw(12)
            << OpCodeName(instruction.opcode) << std::right;
        if (instruction.opcode == OpCode::PushConst || instruction.opcode == OpCode::LoadLocal ||
            instruction.opcode == OpCode::StoreLocal || instruction.opcode == OpCode::Jump ||
            instruction.opcode == OpCode::JumpIfFalse || instruction.opcode == OpCode::Call ||
            instruction.opcode == OpCode::CallHost) out << instruction.operand;
        out << "  ; " << instruction.location.row << ':' << instruction.location.column << '\n';
    }
    return out.str();
}

BytecodeCompiler::BytecodeCompiler(DiagnosticSink& diagnostics) : diagnostics_(diagnostics) {}

BytecodeModule BytecodeCompiler::Compile(AstNode* root, const std::vector<FunctionSignature>& signatures) {
    module_ = {};
    if (!root) return module_;
    std::size_t scriptIndex = 0;
    for (AstNode* node = root->firstChild; node; node = node->nextSibling) {
        if (node->kind != NodeKind::FunctionDecl) continue;
        while (scriptIndex < signatures.size() && signatures[scriptIndex].host) ++scriptIndex;
        for (; scriptIndex < signatures.size(); ++scriptIndex) {
            if (!signatures[scriptIndex].host && signatures[scriptIndex].name == node->token.lexeme) {
                CompileFunction(node, signatures[scriptIndex++]);
                break;
            }
        }
    }
    return std::move(module_);
}

void BytecodeCompiler::CompileFunction(AstNode* node, const FunctionSignature& signature) {
    module_.functions.push_back({signature});
    function_ = &module_.functions.back();
    scopes_.clear();
    scopes_.emplace_back();
    nextLocal_ = 0;
    AstNode* child = node->firstChild;
    while (child && child->kind == NodeKind::Parameter) {
        DeclareLocal(child->token);
        child = child->nextSibling;
    }
    if (child && child->kind == NodeKind::Block) CompileBlock(child, false);
    if (function_->code.empty() || function_->code.back().opcode != OpCode::Return) {
        Emit(OpCode::PushVoid, 0, node);
        Emit(OpCode::Return, 0, node);
    }
    function_->localCount = static_cast<std::size_t>(nextLocal_);
    function_ = nullptr;
}

void BytecodeCompiler::CompileBlock(AstNode* node, bool createScope) {
    if (createScope) scopes_.emplace_back();
    for (AstNode* child = node->firstChild; child; child = child->nextSibling) CompileStatement(child);
    if (createScope) scopes_.pop_back();
}

void BytecodeCompiler::CompileStatement(AstNode* node) {
    Emit(OpCode::Suspend, 0, node);
    switch (node->kind) {
    case NodeKind::Block: CompileBlock(node); break;
    case NodeKind::VarDecl: {
        const auto slot = DeclareLocal(node->token);
        if (node->firstChild) CompileExpression(node->firstChild);
        else Emit(OpCode::PushVoid, 0, node);
        if (node->firstChild && node->firstChild->inferredType == DataType::Int() &&
            node->declaredType == DataType::Float()) Emit(OpCode::ToFloat, 0, node);
        Emit(OpCode::StoreLocal, slot, node);
        break;
    }
    case NodeKind::ExprStmt: CompileExpression(node->firstChild); Emit(OpCode::Pop, 0, node); break;
    case NodeKind::ReturnStmt:
        if (node->firstChild) CompileExpression(node->firstChild);
        else Emit(OpCode::PushVoid, 0, node);
        if (node->firstChild && node->firstChild->inferredType == DataType::Int() &&
            function_->signature.returnType == DataType::Float()) Emit(OpCode::ToFloat, 0, node);
        Emit(OpCode::Return, 0, node);
        break;
    case NodeKind::IfStmt: {
        const auto children = node->Children();
        CompileExpression(children[0]);
        const auto elseJump = Emit(OpCode::JumpIfFalse, -1, node);
        CompileStatement(children[1]);
        if (children.size() == 3) {
            const auto endJump = Emit(OpCode::Jump, -1, node);
            PatchJump(elseJump, function_->code.size());
            CompileStatement(children[2]);
            PatchJump(endJump, function_->code.size());
        } else PatchJump(elseJump, function_->code.size());
        break;
    }
    case NodeKind::WhileStmt: {
        const auto children = node->Children();
        const auto loopStart = function_->code.size();
        CompileExpression(children[0]);
        const auto exitJump = Emit(OpCode::JumpIfFalse, -1, node);
        CompileStatement(children[1]);
        Emit(OpCode::Jump, static_cast<std::int32_t>(loopStart), node);
        PatchJump(exitJump, function_->code.size());
        break;
    }
    default: Error(node, "statement cannot be compiled"); break;
    }
}

void BytecodeCompiler::CompileExpression(AstNode* node) {
    if (!node) return;
    switch (node->kind) {
    case NodeKind::Literal: {
        Value value;
        switch (node->token.kind) {
        case TokenKind::Integer: value = Value(static_cast<std::int32_t>(std::strtol(node->token.lexeme.c_str(), nullptr, 10))); break;
        case TokenKind::Float: value = Value(std::strtof(node->token.lexeme.c_str(), nullptr)); break;
        case TokenKind::String: value = Value(DecodeString(node->token.lexeme)); break;
        case TokenKind::KwTrue: value = Value(true); break;
        case TokenKind::KwFalse: value = Value(false); break;
        default: Error(node, "literal cannot be compiled"); return;
        }
        Emit(OpCode::PushConst, AddConstant(std::move(value)), node);
        break;
    }
    case NodeKind::Identifier: {
        const auto slot = LookupLocal(node->token.lexeme);
        if (!slot) Error(node, "unknown local '" + node->token.lexeme + "'");
        else Emit(OpCode::LoadLocal, *slot, node);
        break;
    }
    case NodeKind::Assign: {
        const auto children = node->Children();
        CompileExpression(children[1]);
        if (children[1]->inferredType == DataType::Int() && children[0]->inferredType == DataType::Float()) {
            Emit(OpCode::ToFloat, 0, node);
        }
        Emit(OpCode::Dup, 0, node);
        const auto slot = LookupLocal(children[0]->token.lexeme);
        if (!slot) Error(node, "unknown assignment target");
        else Emit(OpCode::StoreLocal, *slot, node);
        break;
    }
    case NodeKind::Binary:
        if (node->token.kind == TokenKind::AndAnd || node->token.kind == TokenKind::OrOr) CompileLogical(node);
        else CompileBinary(node);
        break;
    case NodeKind::Unary:
        CompileExpression(node->firstChild);
        if (node->token.kind == TokenKind::Bang) Emit(OpCode::LogicalNot, 0, node);
        else if (node->token.kind == TokenKind::Minus) {
            Emit(node->inferredType == DataType::Float() ? OpCode::NegFloat : OpCode::NegInt, 0, node);
        }
        break;
    case NodeKind::Call: Error(node, "calls are introduced with function frames"); break;
    default: Error(node, "expression cannot be compiled"); break;
    }
}

void BytecodeCompiler::CompileBinary(AstNode* node) {
    const auto children = node->Children();
    const DataType result = node->inferredType;
    CompileExpression(children[0]);
    if (result == DataType::Float() && children[0]->inferredType == DataType::Int()) Emit(OpCode::ToFloat, 0, node);
    if (result == DataType::String() && children[0]->inferredType != DataType::String()) Emit(OpCode::ToString, 0, node);
    CompileExpression(children[1]);
    if (result == DataType::Float() && children[1]->inferredType == DataType::Int()) Emit(OpCode::ToFloat, 0, node);
    if (result == DataType::String() && children[1]->inferredType != DataType::String()) Emit(OpCode::ToString, 0, node);
    const bool floating = children[0]->inferredType == DataType::Float() || children[1]->inferredType == DataType::Float();
    OpCode opcode = OpCode::Nop;
    switch (node->token.kind) {
    case TokenKind::Plus: opcode = result == DataType::String() ? OpCode::Concat : (floating ? OpCode::AddFloat : OpCode::AddInt); break;
    case TokenKind::Minus: opcode = floating ? OpCode::SubFloat : OpCode::SubInt; break;
    case TokenKind::Star: opcode = floating ? OpCode::MulFloat : OpCode::MulInt; break;
    case TokenKind::Slash: opcode = floating ? OpCode::DivFloat : OpCode::DivInt; break;
    case TokenKind::Percent: opcode = OpCode::ModInt; break;
    case TokenKind::EqualEqual: case TokenKind::KwIs: opcode = OpCode::Equal; break;
    case TokenKind::BangEqual: opcode = OpCode::NotEqual; break;
    case TokenKind::Less: opcode = OpCode::Less; break;
    case TokenKind::LessEqual: opcode = OpCode::LessEqual; break;
    case TokenKind::Greater: opcode = OpCode::Greater; break;
    case TokenKind::GreaterEqual: opcode = OpCode::GreaterEqual; break;
    default: Error(node, "binary operator cannot be compiled"); return;
    }
    Emit(opcode, 0, node);
}

void BytecodeCompiler::CompileLogical(AstNode* node) {
    const auto children = node->Children();
    CompileExpression(children[0]);
    const auto branch = Emit(OpCode::JumpIfFalse, -1, node);
    if (node->token.kind == TokenKind::AndAnd) {
        CompileExpression(children[1]);
        const auto end = Emit(OpCode::Jump, -1, node);
        PatchJump(branch, function_->code.size());
        Emit(OpCode::PushConst, AddConstant(Value(false)), node);
        PatchJump(end, function_->code.size());
    } else {
        Emit(OpCode::PushConst, AddConstant(Value(true)), node);
        const auto end = Emit(OpCode::Jump, -1, node);
        PatchJump(branch, function_->code.size());
        CompileExpression(children[1]);
        PatchJump(end, function_->code.size());
    }
}

std::size_t BytecodeCompiler::Emit(OpCode opcode, std::int32_t operand, const AstNode* node) {
    function_->code.push_back({opcode, operand, node ? node->token.location : SourceLocation{}});
    return function_->code.size() - 1;
}

void BytecodeCompiler::PatchJump(std::size_t instruction, std::size_t target) {
    if (instruction >= function_->code.size()) return;
    function_->code[instruction].operand = static_cast<std::int32_t>(target);
}

std::int32_t BytecodeCompiler::AddConstant(Value value) {
    function_->constants.push_back(std::move(value));
    return static_cast<std::int32_t>(function_->constants.size() - 1);
}

std::optional<std::int32_t> BytecodeCompiler::LookupLocal(std::string_view name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(std::string(name));
        if (found != scope->end()) return found->second;
    }
    return std::nullopt;
}

std::int32_t BytecodeCompiler::DeclareLocal(const Token& name) {
    const auto slot = nextLocal_++;
    scopes_.back()[name.lexeme] = slot;
    return slot;
}

void BytecodeCompiler::Error(const AstNode* node, std::string message) {
    diagnostics_.Report(node ? node->token.location : SourceLocation{}, Severity::Error, std::move(message));
}

} // namespace mini_as
