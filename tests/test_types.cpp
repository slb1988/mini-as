#include "test.hpp"
#include "mini_as/type_checker.hpp"

namespace {
bool CheckSource(std::string_view source, mini_as::DiagnosticSink& diagnostics) {
    mini_as::Tokenizer tokenizer("types", source, diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    return !diagnostics.HasErrors() && checker.Check(tree.root);
}
}

TEST_CASE(type_checker_accepts_scopes_and_numeric_conversion) {
    mini_as::DiagnosticSink diagnostics;
    CHECK(CheckSource("float calc(int x) { float y = x; { int y = 2; } return y + 0.5; }", diagnostics));
}

TEST_CASE(type_checker_rejects_unknown_and_wrong_return) {
    mini_as::DiagnosticSink diagnostics;
    CHECK(!CheckSource("int bad() { missing = 1; return 1.5; }", diagnostics));
    CHECK(diagnostics.All().size() >= 2);
}

TEST_CASE(type_checker_promotes_and_converts_integer_widths) {
    mini_as::DiagnosticSink diagnostics;
    CHECK(CheckSource(
        "int result(int8 a, uint16 b, int64 c, uint64 d) { "
        "uint mixed = a + b; int64 wide = mixed + c; uint64 widest = wide + d; return widest; }",
        diagnostics));
    CHECK(mini_as::CommonNumericType(mini_as::DataType::Int8(), mini_as::DataType::UInt16()) ==
          mini_as::DataType::Int());
    CHECK(mini_as::CommonNumericType(mini_as::DataType::Int64(), mini_as::DataType::UInt64()) ==
          mini_as::DataType::UInt64());
}

TEST_CASE(type_checker_resolves_enum_constant_expressions_and_preserves_identity) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("types",
        "enum Color { Red = 2, Green, Blue = Green + 2 } "
        "Color choose() { Color value = Blue; return value; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    CHECK(checker.Enums().size() == 1);
    CHECK(checker.Enums()[0].values.size() == 3);
    CHECK(checker.Enums()[0].values[0].value == 2);
    CHECK(checker.Enums()[0].values[1].value == 3);
    CHECK(checker.Enums()[0].values[2].value == 5);
    CHECK(tree.root->Children()[1]->declaredType == mini_as::DataType::Enum("Color"));
}

TEST_CASE(type_checker_exposes_typedef_metadata_with_canonical_storage_type) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("types",
        "typedef uint64 EntityId; EntityId identity(EntityId value) { return value; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    CHECK(checker.Typedefs().size() == 1);
    CHECK(checker.Typedefs()[0].name == "EntityId");
    CHECK(checker.Typedefs()[0].underlyingType == mini_as::DataType::UInt64());
    CHECK(checker.Functions().back().returnType == mini_as::DataType::UInt64());
}

