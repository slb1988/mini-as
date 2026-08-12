#pragma once

#include "mini_as/parser.hpp"

#include <functional>
#include <optional>

namespace mini_as {

std::optional<Value> DecodeNumericLiteral(const Token& token);

class ConstantExpressionEvaluator {
public:
    using IdentifierResolver = std::function<std::optional<Value>(std::string_view)>;

    explicit ConstantExpressionEvaluator(IdentifierResolver resolver = {});
    std::optional<Value> Evaluate(const AstNode* expression) const;

private:
    IdentifierResolver resolver_;
};

} // namespace mini_as
