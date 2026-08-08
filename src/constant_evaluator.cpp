#include "mini_as/constant_evaluator.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

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

double AsDouble(const Value& value) {
    if (value.Type() == DataType::Double()) return value.As<double>();
    if (value.Type() == DataType::Float()) return value.As<float>();
    if (value.Type().IsSignedInteger()) return static_cast<double>(value.SignedInteger());
    return static_cast<double>(value.UnsignedInteger());
}

std::optional<Value> EvaluateBinary(TokenKind operation, const Value& left, const Value& right) {
    const bool floating = !left.Type().IsInteger() || !right.Type().IsInteger();
    const bool doublePrecision = left.Type() == DataType::Double() || right.Type() == DataType::Double();
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
        if (left.Type().IsInteger() && right.Type().IsInteger()) {
            const DataType common = CommonNumericType(left.Type(), right.Type());
            const Value convertedLeft = ConvertInteger(left, common);
            const Value convertedRight = ConvertInteger(right, common);
            equal = common.IsSignedInteger()
                ? convertedLeft.SignedInteger() == convertedRight.SignedInteger()
                : convertedLeft.UnsignedInteger() == convertedRight.UnsignedInteger();
        } else if (left.Type().IsNumeric() && right.Type().IsNumeric()) {
            equal = AsDouble(left) == AsDouble(right);
        }
        else if (left.Type() == right.Type()) equal = left == right;
        else return std::nullopt;
        return Value(operation == TokenKind::EqualEqual ? equal : !equal);
    }
    if (!left.Type().IsNumeric() || !right.Type().IsNumeric()) return std::nullopt;
    if (floating) {
        const double floatLeft = AsDouble(left), floatRight = AsDouble(right);
        if (operation == TokenKind::Less) return Value(floatLeft < floatRight);
        if (operation == TokenKind::LessEqual) return Value(floatLeft <= floatRight);
        if (operation == TokenKind::Greater) return Value(floatLeft > floatRight);
        if (operation == TokenKind::GreaterEqual) return Value(floatLeft >= floatRight);
        const auto result = [&](double value) {
            return doublePrecision ? Value(value) : Value(static_cast<float>(value));
        };
        if (operation == TokenKind::Plus) return result(floatLeft + floatRight);
        if (operation == TokenKind::Minus) return result(floatLeft - floatRight);
        if (operation == TokenKind::Star) return result(floatLeft * floatRight);
        if (operation == TokenKind::Slash && floatRight != 0.0) return result(floatLeft / floatRight);
        if (operation == TokenKind::StarStar) return result(std::pow(floatLeft, floatRight));
        return std::nullopt;
    }
    const DataType common = CommonNumericType(left.Type(), right.Type());
    const Value convertedLeft = ConvertInteger(left, common);
    const Value convertedRight = ConvertInteger(right, common);
    if (operation == TokenKind::Less || operation == TokenKind::LessEqual ||
        operation == TokenKind::Greater || operation == TokenKind::GreaterEqual) {
        if (common.IsSignedInteger()) {
            const auto a = convertedLeft.SignedInteger(), b = convertedRight.SignedInteger();
            if (operation == TokenKind::Less) return Value(a < b);
            if (operation == TokenKind::LessEqual) return Value(a <= b);
            if (operation == TokenKind::Greater) return Value(a > b);
            return Value(a >= b);
        }
        const auto a = convertedLeft.UnsignedInteger(), b = convertedRight.UnsignedInteger();
        if (operation == TokenKind::Less) return Value(a < b);
        if (operation == TokenKind::LessEqual) return Value(a <= b);
        if (operation == TokenKind::Greater) return Value(a > b);
        return Value(a >= b);
    }
    const auto a = convertedLeft.UnsignedInteger(), b = convertedRight.UnsignedInteger();
    if (operation == TokenKind::Amp) return Value::Integer(left.Type(), a & b);
    if (operation == TokenKind::Pipe) return Value::Integer(left.Type(), a | b);
    if (operation == TokenKind::Caret) return Value::Integer(left.Type(), a ^ b);
    if (operation == TokenKind::ShiftLeft)
        return Value::Integer(left.Type(), a << (b & (left.Type().IntegerBits() - 1)));
    if (operation == TokenKind::ShiftRight)
        return Value::Integer(left.Type(), a >> (b & (left.Type().IntegerBits() - 1)));
    if (operation == TokenKind::ShiftRightArithmetic) {
        const auto count = static_cast<unsigned>(b & (left.Type().IntegerBits() - 1));
        return left.Type().IsSignedInteger()
            ? Value::Integer(left.Type(), static_cast<std::uint64_t>(convertedLeft.SignedInteger() >> count))
            : Value::Integer(left.Type(), a >> count);
    }
    if (operation == TokenKind::Plus) return Value::Integer(common, a + b);
    if (operation == TokenKind::Minus) return Value::Integer(common, a - b);
    if (operation == TokenKind::Star) return Value::Integer(common, a * b);
    if (operation == TokenKind::StarStar) {
        if (common.IsSignedInteger() && convertedRight.SignedInteger() < 0)
            return convertedLeft.UnsignedInteger() == 0 ? std::nullopt
                                                        : std::optional<Value>{Value::Integer(common, 0)};
        std::uint64_t exponent = common.IsSignedInteger()
            ? static_cast<std::uint64_t>(convertedRight.SignedInteger()) : b;
        if (a == 0 && exponent == 0) return std::nullopt;
        std::uint64_t base = a, result = 1;
        while (exponent) {
            if (exponent & 1) result *= base;
            exponent >>= 1;
            if (exponent) base *= base;
        }
        return Value::Integer(common, result);
    }
    if ((operation == TokenKind::Slash || operation == TokenKind::Percent) && b == 0)
        return std::nullopt;
    if (operation == TokenKind::Slash) {
        if (common.IsSignedInteger()) {
            const auto signedLeft = convertedLeft.SignedInteger();
            const auto signedRight = convertedRight.SignedInteger();
            if (signedLeft == std::numeric_limits<std::int64_t>::min() && signedRight == -1)
                return Value::Integer(common, static_cast<std::uint64_t>(signedLeft));
            return Value::Integer(common, static_cast<std::uint64_t>(signedLeft / signedRight));
        }
        return Value::Integer(common, a / b);
    }
    if (operation == TokenKind::Percent) {
        if (common.IsSignedInteger()) {
            const auto signedLeft = convertedLeft.SignedInteger();
            const auto signedRight = convertedRight.SignedInteger();
            if (signedLeft == std::numeric_limits<std::int64_t>::min() && signedRight == -1)
                return Value::Integer(common, 0);
            return Value::Integer(common, static_cast<std::uint64_t>(signedLeft % signedRight));
        }
        return Value::Integer(common, a % b);
    }
    return std::nullopt;
}

} // namespace

std::optional<Value> DecodeNumericLiteral(const Token& token) {
    if (token.kind == TokenKind::Float) {
        std::string text = token.lexeme;
        if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) text.pop_back();
        return Value(std::strtof(text.c_str(), nullptr));
    }
    if (token.kind == TokenKind::Double)
        return Value(std::strtod(token.lexeme.c_str(), nullptr));
    if (token.kind != TokenKind::Integer && token.kind != TokenKind::Bits) return std::nullopt;

    int base = 10;
    std::string_view digits = token.lexeme;
    if (token.kind == TokenKind::Bits) {
        if (digits.size() < 3) return std::nullopt;
        switch (digits[1]) {
        case 'b': case 'B': base = 2; break;
        case 'o': case 'O': base = 8; break;
        case 'd': case 'D': base = 10; break;
        case 'x': case 'X': base = 16; break;
        default: return std::nullopt;
        }
        digits.remove_prefix(2);
    }
    std::uint64_t parsed = 0;
    const auto conversion = std::from_chars(digits.data(), digits.data() + digits.size(), parsed, base);
    if (conversion.ec != std::errc{} || conversion.ptr != digits.data() + digits.size())
        return std::nullopt;
    if (token.kind == TokenKind::Bits)
        return Value::Integer(parsed <= std::numeric_limits<std::uint32_t>::max()
                              ? DataType::UInt() : DataType::UInt64(), parsed);
    if (parsed <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
        return Value(static_cast<std::int32_t>(parsed));
    if (parsed <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return Value::Integer(DataType::Int64(), parsed);
    return Value::Integer(DataType::UInt64(), parsed);
}

std::optional<Value> ConstantExpressionEvaluator::Evaluate(const AstNode* expression) const {
    if (!expression) return std::nullopt;
    if (expression->kind == NodeKind::Literal) {
        switch (expression->token.kind) {
        case TokenKind::Integer: case TokenKind::Bits:
        case TokenKind::Float: case TokenKind::Double:
            return DecodeNumericLiteral(expression->token);
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
        if (expression->token.kind == TokenKind::Tilde && operand->Type().IsInteger())
            return Value::Integer(operand->Type(), ~operand->UnsignedInteger());
        if (expression->token.kind == TokenKind::Plus && operand->Type().IsNumeric()) return operand;
        if (expression->token.kind == TokenKind::Minus) {
            if (operand->Type().IsInteger())
                return Value::Integer(operand->Type(), std::uint64_t{0} - operand->UnsignedInteger());
            if (operand->Type() == DataType::Float()) return Value(-operand->As<float>());
            if (operand->Type() == DataType::Double()) return Value(-operand->As<double>());
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
