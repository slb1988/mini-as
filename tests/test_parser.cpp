#include "test.hpp"
#include "mini_as/parser.hpp"

TEST_CASE(parser_builds_function_tree_with_precedence) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "int add(int a, int b) { return a + b * 2; }", diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    CHECK(tree.root->Children().size() == 1);
    auto* function = tree.root->firstChild;
    CHECK(function->kind == mini_as::NodeKind::FunctionDecl);
    CHECK(function->Children().size() == 3);
    auto* expression = function->Children()[2]->firstChild->firstChild;
    CHECK(expression->token.kind == mini_as::TokenKind::Plus);
    CHECK(expression->Children()[1]->token.kind == mini_as::TokenKind::Star);
}

