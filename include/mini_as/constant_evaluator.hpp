#pragma once

#include "mini_as/parser.hpp"

#include <optional>

namespace mini_as {

std::optional<Value> DecodeNumericLiteral(const Token& token);

class ConstantExpressionEvaluator {
public:
    std::optional<Value> Evaluate(const AstNode* expression) const;
};

} // namespace mini_as
