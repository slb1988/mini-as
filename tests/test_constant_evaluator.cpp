#include "test.hpp"
#include "mini_as/constant_evaluator.hpp"
#include "mini_as/type_checker.hpp"

namespace {
std::optional<mini_as::Value> EvaluateReturn(std::string_view expression,
                                             mini_as::DiagnosticSink& diagnostics) {
    mini_as::Tokenizer tokenizer("constant", "int value() { return " + std::string(expression) + "; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    if (!checker.Check(tree.root)) return std::nullopt;
    const auto* returnExpression = tree.root->firstChild->Children().back()->firstChild->firstChild;
    return mini_as::ConstantExpressionEvaluator{}.Evaluate(returnExpression);
}
}

TEST_CASE(constant_evaluator_folds_typed_expression_trees) {
    mini_as::DiagnosticSink diagnostics;
    auto value = EvaluateReturn("(2 + 3) * 4 - 1", diagnostics);
    CHECK(!diagnostics.HasErrors());
    CHECK(value.has_value());
    CHECK(value->As<std::int32_t>() == 19);
}

TEST_CASE(constant_evaluator_rejects_runtime_values_and_invalid_arithmetic) {
    mini_as::DiagnosticSink diagnostics;
    auto runtimeValue = EvaluateReturn("unknown()", diagnostics);
    CHECK(!runtimeValue.has_value());

    diagnostics.Clear();
    auto divisionByZero = EvaluateReturn("1 / 0", diagnostics);
    CHECK(!divisionByZero.has_value());
}

TEST_CASE(numeric_literal_decoder_preserves_official_types_and_values) {
    mini_as::Token bits{mini_as::TokenKind::Bits, "0xFFFFFFFF", {}};
    auto unsignedValue = mini_as::DecodeNumericLiteral(bits);
    CHECK(unsignedValue.has_value());
    CHECK(unsignedValue->Type() == mini_as::DataType::UInt());
    CHECK(unsignedValue->UnsignedInteger() == 0xFFFFFFFFull);

    mini_as::Token wide{mini_as::TokenKind::Integer, "2147483648", {}};
    auto signedWide = mini_as::DecodeNumericLiteral(wide);
    CHECK(signedWide.has_value());
    CHECK(signedWide->Type() == mini_as::DataType::Int64());
    CHECK(signedWide->SignedInteger() == 2147483648ll);

    mini_as::Token real{mini_as::TokenKind::Double, "1.25e2", {}};
    CHECK(mini_as::DecodeNumericLiteral(real)->As<double>() == 125.0);
    mini_as::Token single{mini_as::TokenKind::Float, "1.25f", {}};
    CHECK(mini_as::DecodeNumericLiteral(single)->As<float>() == 1.25f);
}
