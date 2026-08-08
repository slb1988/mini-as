#pragma once

#include "mini_as/parser.hpp"

#include <optional>

namespace mini_as {

class ConstantExpressionEvaluator {
public:
    std::optional<Value> Evaluate(const AstNode* expression) const;
};

} // namespace mini_as
