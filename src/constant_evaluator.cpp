#include "mini_as/constant_evaluator.hpp"

#include <cstdlib>

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

float AsFloat(const Value& value) {
    return value.Type() == DataType::Float()
        ? value.As<float>() : static_cast<float>(value.As<std::int32_t>());
}

std::optional<Value> EvaluateBinary(TokenKind operation, const Value& left, const Value& right) {
    const bool floating = left.Type() == DataType::Float() || right.Type() == DataType::Float();
    if (operation == TokenKind::Plus &&
        (left.Type() == DataType::String() || right.Type() == DataType::String())) {
        return Value(left.ToString() + right.ToString());
    }
    if (operation == TokenKind::AndAnd || operation == TokenKind::OrOr) {
        if (left.Type() != DataType::Bool() || right.Type() != DataType::Bool()) return std::nullopt;
        return Value(operation == TokenKind::AndAnd
            ? left.As<bool>() && right.As<bool>() : left.As<bool>() || right.As<bool>());
    }
    if (operation == TokenKind::EqualEqual || operation == TokenKind::BangEqual) {
        bool equal = false;
        if (left.Type().IsNumeric() && right.Type().IsNumeric()) equal = AsFloat(left) == AsFloat(right);
        else if (left.Type() == right.Type()) equal = left == right;
        else return std::nullopt;
        return Value(operation == TokenKind::EqualEqual ? equal : !equal);
    }
    if (!left.Type().IsNumeric() || !right.Type().IsNumeric()) return std::nullopt;
    const float floatLeft = AsFloat(left), floatRight = AsFloat(right);
    if (operation == TokenKind::Less) return Value(floatLeft < floatRight);
    if (operation == TokenKind::LessEqual) return Value(floatLeft <= floatRight);
    if (operation == TokenKind::Greater) return Value(floatLeft > floatRight);
    if (operation == TokenKind::GreaterEqual) return Value(floatLeft >= floatRight);
    if (floating) {
        if (operation == TokenKind::Plus) return Value(floatLeft + floatRight);
        if (operation == TokenKind::Minus) return Value(floatLeft - floatRight);
        if (operation == TokenKind::Star) return Value(floatLeft * floatRight);
        if (operation == TokenKind::Slash && floatRight != 0.0f) return Value(floatLeft / floatRight);
        return std::nullopt;
    }
    const auto intLeft = left.As<std::int32_t>(), intRight = right.As<std::int32_t>();
    if (operation == TokenKind::Plus) return Value(intLeft + intRight);
    if (operation == TokenKind::Minus) return Value(intLeft - intRight);
    if (operation == TokenKind::Star) return Value(intLeft * intRight);
    if (operation == TokenKind::Slash && intRight != 0) return Value(intLeft / intRight);
    if (operation == TokenKind::Percent && intRight != 0) return Value(intLeft % intRight);
    return std::nullopt;
}

} // namespace

std::optional<Value> ConstantExpressionEvaluator::Evaluate(const AstNode* expression) const {
    if (!expression) return std::nullopt;
    if (expression->kind == NodeKind::Literal) {
        switch (expression->token.kind) {
        case TokenKind::Integer:
            return Value(static_cast<std::int32_t>(
                std::strtol(expression->token.lexeme.c_str(), nullptr, 10)));
        case TokenKind::Float: return Value(std::strtof(expression->token.lexeme.c_str(), nullptr));
        case TokenKind::String: return Value(DecodeString(expression->token.lexeme));
        case TokenKind::KwTrue: return Value(true);
        case TokenKind::KwFalse: return Value(false);
        default: return std::nullopt;
        }
    }
    if (expression->kind == NodeKind::Unary) {
        auto operand = Evaluate(expression->firstChild);
        if (!operand) return std::nullopt;
        if (expression->token.kind == TokenKind::Bang && operand->Type() == DataType::Bool())
            return Value(!operand->As<bool>());
        if (expression->token.kind == TokenKind::Plus && operand->Type().IsNumeric()) return operand;
        if (expression->token.kind == TokenKind::Minus) {
            if (operand->Type() == DataType::Int()) return Value(-operand->As<std::int32_t>());
            if (operand->Type() == DataType::Float()) return Value(-operand->As<float>());
        }
        return std::nullopt;
    }
    if (expression->kind == NodeKind::Binary) {
        auto left = Evaluate(expression->firstChild);
        if (!left) return std::nullopt;
        if (expression->token.kind == TokenKind::AndAnd && left->Type() == DataType::Bool() && !left->As<bool>())
            return Value(false);
        if (expression->token.kind == TokenKind::OrOr && left->Type() == DataType::Bool() && left->As<bool>())
            return Value(true);
        auto right = Evaluate(expression->firstChild->nextSibling);
        if (!right) return std::nullopt;
        return EvaluateBinary(expression->token.kind, *left, *right);
    }
    if (expression->kind == NodeKind::Conditional) {
        auto condition = Evaluate(expression->firstChild);
        if (!condition || condition->Type() != DataType::Bool()) return std::nullopt;
        const AstNode* branch = condition->As<bool>()
            ? expression->firstChild->nextSibling
            : expression->firstChild->nextSibling->nextSibling;
        return Evaluate(branch);
    }
    return std::nullopt;
}

} // namespace mini_as
