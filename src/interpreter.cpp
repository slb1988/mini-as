#include "mini_as/interpreter.hpp"
#include "mini_as/constant_evaluator.hpp"

#include <cmath>
#include <cstdlib>
#include <utility>

namespace mini_as {
namespace {

std::string DecodeString(std::string_view lexeme) {
    std::string result;
    for (std::size_t index = 1; index + 1 < lexeme.size(); ++index) {
        char ch = lexeme[index];
        if (ch == '\\' && index + 1 < lexeme.size() - 1) {
            ch = lexeme[++index];
            if (ch == 'n') result.push_back('\n');
            else if (ch == 't') result.push_back('\t');
            else if (ch == 'r') result.push_back('\r');
            else result.push_back(ch);
        } else result.push_back(ch);
    }
    return result;
}

double Number(const Value& value) {
    if (value.Type().IsSignedInteger()) return static_cast<double>(value.SignedInteger());
    if (value.Type().IsUnsignedInteger()) return static_cast<double>(value.UnsignedInteger());
    if (value.Type() == DataType::Float()) return value.As<float>();
    return value.As<double>();
}

} // namespace

TreeInterpreter::TreeInterpreter(DiagnosticSink& diagnostics) : diagnostics_(diagnostics) {}

void TreeInterpreter::RegisterFunction(std::string name, TreeHostFunction function) {
    functions_[std::move(name)] = std::move(function);
}

Value TreeInterpreter::Execute(AstNode* root) {
    Value last;
    if (!root) return last;
    if (root->kind != NodeKind::Program) return Evaluate(root);
    for (AstNode* child = root->firstChild; child; child = child->nextSibling) last = Evaluate(child);
    return last;
}

Value TreeInterpreter::Evaluate(AstNode* node) {
    if (!node) return {};
    switch (node->kind) {
    case NodeKind::Program: return Execute(node);
    case NodeKind::ExprStmt: return Evaluate(node->firstChild);
    case NodeKind::Literal: return DecodeLiteral(node->token);
    case NodeKind::Binary: return EvaluateBinary(node);
    case NodeKind::Unary: return EvaluateUnary(node);
    case NodeKind::Call: return EvaluateCall(node);
    default: RuntimeError(node, "construct is not executable in milestone M0"); return {};
    }
}

Value TreeInterpreter::EvaluateBinary(AstNode* node) {
    const auto children = node->Children();
    Value left = Evaluate(children[0]);
    Value right = Evaluate(children[1]);
    if (node->token.kind == TokenKind::Plus &&
        (left.Type() == DataType::String() || right.Type() == DataType::String())) {
        return Value(left.ToString() + right.ToString());
    }
    if (!left.Type().IsNumeric() || !right.Type().IsNumeric()) {
        RuntimeError(node, "operator requires numeric operands"); return {};
    }
    if (!left.Type().IsInteger() || !right.Type().IsInteger()) {
        const double a = Number(left), b = Number(right);
        switch (node->token.kind) {
        case TokenKind::Plus: return Value(a + b);
        case TokenKind::Minus: return Value(a - b);
        case TokenKind::Star: return Value(a * b);
        case TokenKind::Slash:
            if (b == 0) { RuntimeError(node, "division by zero"); return {}; }
            return Value(a / b);
        default: break;
        }
    } else {
        const auto a = left.As<std::int32_t>(), b = right.As<std::int32_t>();
        switch (node->token.kind) {
        case TokenKind::Plus: return Value(a + b);
        case TokenKind::Minus: return Value(a - b);
        case TokenKind::Star: return Value(a * b);
        case TokenKind::Slash:
            if (b == 0) { RuntimeError(node, "division by zero"); return {}; }
            return Value(a / b);
        case TokenKind::Percent:
            if (b == 0) { RuntimeError(node, "division by zero"); return {}; }
            return Value(a % b);
        default: break;
        }
    }
    RuntimeError(node, "unsupported binary operator");
    return {};
}

Value TreeInterpreter::EvaluateUnary(AstNode* node) {
    Value operand = Evaluate(node->firstChild);
    if (node->token.kind == TokenKind::Minus) {
        if (operand.Type() == DataType::Int()) return Value(-operand.As<std::int32_t>());
        if (operand.Type() == DataType::Float()) return Value(-operand.As<float>());
        if (operand.Type() == DataType::Double()) return Value(-operand.As<double>());
    }
    if (node->token.kind == TokenKind::Plus && operand.Type().IsNumeric()) return operand;
    if (node->token.kind == TokenKind::Bang && operand.Type() == DataType::Bool()) {
        return Value(!operand.As<bool>());
    }
    RuntimeError(node, "unsupported unary operator");
    return {};
}

Value TreeInterpreter::EvaluateCall(AstNode* node) {
    AstNode* callee = node->firstChild;
    if (!callee || callee->kind != NodeKind::Identifier) {
        RuntimeError(node, "callee must be a function name"); return {};
    }
    const auto found = functions_.find(callee->token.lexeme);
    if (found == functions_.end()) {
        RuntimeError(node, "unknown host function '" + callee->token.lexeme + "'"); return {};
    }
    std::vector<Value> arguments;
    for (AstNode* argument = callee->nextSibling; argument; argument = argument->nextSibling) {
        arguments.push_back(Evaluate(argument));
    }
    try { return found->second(arguments); }
    catch (const std::exception& error) { RuntimeError(node, error.what()); return {}; }
}

Value TreeInterpreter::DecodeLiteral(const Token& token) {
    switch (token.kind) {
    case TokenKind::Integer: case TokenKind::Bits:
    case TokenKind::Float: case TokenKind::Double: {
        const auto value = DecodeNumericLiteral(token);
        if (value) return *value;
        RuntimeError(nullptr, "numeric literal is out of range");
        return {};
    }
    case TokenKind::String: return Value(DecodeString(token.lexeme));
    case TokenKind::KwTrue: return Value(true);
    case TokenKind::KwFalse: return Value(false);
    default: RuntimeError(nullptr, "unsupported literal"); return {};
    }
}

void TreeInterpreter::RuntimeError(const AstNode* node, std::string message) {
    diagnostics_.Report(node ? node->token.location : SourceLocation{}, Severity::Error, std::move(message));
}

bool RunTreeScript(std::string_view source, TreeInterpreter& interpreter,
                   DiagnosticSink& diagnostics, std::string section) {
    Tokenizer tokenizer(std::move(section), source, diagnostics);
    Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    if (diagnostics.HasErrors()) return false;
    interpreter.Execute(tree.root);
    return !diagnostics.HasErrors();
}

} // namespace mini_as

