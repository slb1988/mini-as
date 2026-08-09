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

TEST_CASE(parser_groups_multiple_declarations_with_independent_initializers) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "int f() { int a = 1, b, c = a + 2; return c; }", diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    auto* declarations = tree.root->firstChild->Children().back()->firstChild;
    CHECK(declarations->kind == mini_as::NodeKind::DeclList);
    const auto variables = declarations->Children();
    CHECK(variables.size() == 3);
    CHECK(variables[0]->token.lexeme == "a");
    CHECK(variables[0]->firstChild != nullptr);
    CHECK(variables[1]->token.lexeme == "b");
    CHECK(variables[1]->firstChild == nullptr);
    CHECK(variables[2]->token.lexeme == "c");
}

TEST_CASE(parser_marks_const_auto_declarations_for_type_inference) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "int f() { const auto answer = 42; return answer; }", diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    auto* declaration = tree.root->firstChild->Children().back()->firstChild;
    CHECK(declaration->kind == mini_as::NodeKind::VarDecl);
    CHECK(declaration->isAuto);
    CHECK(declaration->isConst);
}

TEST_CASE(parser_builds_four_clause_for_statement) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "int f() { for (int i = 0; i < 3; i = i + 1) { } return 0; }", diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    auto* loop = tree.root->firstChild->Children().back()->firstChild;
    CHECK(loop->kind == mini_as::NodeKind::ForStmt);
    const auto clauses = loop->Children();
    CHECK(clauses.size() == 4);
    CHECK(clauses[0]->kind == mini_as::NodeKind::VarDecl);
    CHECK(clauses[1]->kind == mini_as::NodeKind::Binary);
    CHECK(clauses[2]->kind == mini_as::NodeKind::Assign);
    CHECK(clauses[3]->kind == mini_as::NodeKind::Block);
}

TEST_CASE(parser_preserves_switch_clause_order) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse",
        "int f(int value) { switch (value) { case 1: value = 2; case 2: return value; default: return 0; } }",
        diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    auto* switchNode = tree.root->firstChild->Children().back()->firstChild;
    CHECK(switchNode->kind == mini_as::NodeKind::SwitchStmt);
    const auto children = switchNode->Children();
    CHECK(children.size() == 4);
    CHECK(children[1]->kind == mini_as::NodeKind::CaseClause);
    CHECK(children[2]->kind == mini_as::NodeKind::CaseClause);
    CHECK(children[3]->kind == mini_as::NodeKind::DefaultClause);
}

TEST_CASE(parser_makes_conditional_expressions_right_associative) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "int f() { return false ? 1 : true ? 2 : 3; }", diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    auto* expression = tree.root->firstChild->Children().back()->firstChild->firstChild;
    CHECK(expression->kind == mini_as::NodeKind::Conditional);
    CHECK(expression->Children()[2]->kind == mini_as::NodeKind::Conditional);
}

TEST_CASE(parser_builds_typed_enum_values_with_optional_initializers) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse",
        "enum Color { Red = 2, Green, Blue = Green + 2 }; Color current = Blue;",
        diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    const auto declarations = tree.root->Children();
    CHECK(declarations.size() == 2);
    CHECK(declarations[0]->kind == mini_as::NodeKind::EnumDecl);
    const auto values = declarations[0]->Children();
    CHECK(values.size() == 3);
    CHECK(values[0]->kind == mini_as::NodeKind::EnumValue);
    CHECK(values[0]->firstChild->kind == mini_as::NodeKind::Literal);
    CHECK(values[1]->firstChild == nullptr);
    CHECK(values[2]->firstChild->kind == mini_as::NodeKind::Binary);
    CHECK(declarations[1]->declaredType == mini_as::DataType::Enum("Color"));
}

