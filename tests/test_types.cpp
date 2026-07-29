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

