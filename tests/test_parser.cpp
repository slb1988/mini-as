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

TEST_CASE(parser_resolves_primitive_typedefs_in_all_declarations) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse",
        "typedef int Score; Score total = 40; Score add(Score value) { Score next = value + 2; return next; }",
        diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    const auto declarations = tree.root->Children();
    CHECK(declarations[0]->kind == mini_as::NodeKind::TypedefDecl);
    CHECK(declarations[0]->declaredType == mini_as::DataType::Int());
    CHECK(declarations[1]->declaredType == mini_as::DataType::Int());
    CHECK(declarations[2]->declaredType == mini_as::DataType::Int());
    CHECK(declarations[2]->firstChild->declaredType == mini_as::DataType::Int());
    CHECK(declarations[2]->Children().back()->firstChild->declaredType == mini_as::DataType::Int());
}

TEST_CASE(parser_rejects_nonprimitive_typedef_sources) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "class Box {} typedef Box Alias;", diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    parser.Parse();
    CHECK(diagnostics.HasErrors());
    CHECK(diagnostics.All().back().message.find("built-in primitive") != std::string::npos);
}

TEST_CASE(parser_preserves_namespace_hierarchy_and_qualified_declarations) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse",
        "namespace Outer { int value = 1; namespace Inner { enum Code { Ok } "
        "int read() { return Outer::value + Ok; } } }",
        diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    auto* outer = tree.root->firstChild;
    CHECK(outer->kind == mini_as::NodeKind::NamespaceDecl);
    CHECK(outer->token.lexeme == "Outer");
    CHECK(outer->firstChild->token.lexeme == "Outer::value");
    auto* inner = outer->firstChild->nextSibling;
    CHECK(inner->kind == mini_as::NodeKind::NamespaceDecl);
    CHECK(inner->token.lexeme == "Outer::Inner");
    CHECK(inner->firstChild->token.lexeme == "Outer::Inner::Code");
    CHECK(inner->firstChild->nextSibling->token.lexeme == "Outer::Inner::read");
}

TEST_CASE(parser_attaches_default_expressions_to_parameters) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "int add(int value, int delta = 2) { return value + delta; }",
                             diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    const auto parameters = tree.root->firstChild->Children();
    CHECK(parameters[0]->kind == mini_as::NodeKind::Parameter);
    CHECK(parameters[0]->firstChild == nullptr);
    CHECK(parameters[1]->kind == mini_as::NodeKind::Parameter);
    CHECK(parameters[1]->firstChild->kind == mini_as::NodeKind::Literal);
}

TEST_CASE(parser_rejects_required_parameters_after_defaults) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "int bad(int first = 1, int second) { return second; }", diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    parser.Parse();
    CHECK(diagnostics.HasErrors());
    CHECK(diagnostics.All().back().message.find("must also have defaults") != std::string::npos);
}

TEST_CASE(parser_wraps_named_arguments_and_rejects_late_positionals) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "int main() { return add(second: 2, first: 40); }", diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    auto* call = tree.root->firstChild->Children().back()->firstChild->firstChild;
    CHECK(call->kind == mini_as::NodeKind::Call);
    CHECK(call->Children()[1]->kind == mini_as::NodeKind::NamedArgument);
    CHECK(call->Children()[1]->token.lexeme == "second");

    mini_as::DiagnosticSink badDiagnostics;
    mini_as::Tokenizer badLexer("parse", "int main() { return add(second: 2, 40); }", badDiagnostics);
    mini_as::Parser badParser(badLexer.ScanAll(), badDiagnostics);
    badParser.Parse();
    CHECK(badDiagnostics.HasErrors());
}

TEST_CASE(parser_preserves_in_out_and_inout_parameter_modes) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse",
        "void update(int &in source, int &out result, int &inout total, int value) {}",
        diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    const auto parameters = tree.root->firstChild->Children();
    CHECK(parameters[0]->parameterMode == mini_as::ParameterMode::In);
    CHECK(parameters[1]->parameterMode == mini_as::ParameterMode::Out);
    CHECK(parameters[2]->parameterMode == mini_as::ParameterMode::InOut);
    CHECK(parameters[3]->parameterMode == mini_as::ParameterMode::Value);
}

TEST_CASE(parser_preserves_mutable_and_const_reference_returns) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse",
        "int &access() { return value; } const int &read() { return value; }",
        diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    const auto declarations = tree.root->Children();
    CHECK(declarations[0]->returnsReference);
    CHECK(!declarations[0]->returnReferenceConst);
    CHECK(declarations[1]->returnsReference);
    CHECK(declarations[1]->returnReferenceConst);
}

TEST_CASE(parser_marks_script_destructors) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse", "class Resource { ~Resource() {} }", diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    auto* destructor = tree.root->firstChild->firstChild;
    CHECK(destructor->kind == mini_as::NodeKind::FunctionDecl);
    CHECK(destructor->token.lexeme == "~Resource");
    CHECK(destructor->isDestructor);
    CHECK(destructor->declaredType == mini_as::DataType::Void());
}

TEST_CASE(parser_preserves_class_and_interface_inheritance_lists) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("parse",
        "class Base {} interface IValue { int get(); } class Derived : Base, IValue {}",
        diagnostics);
    mini_as::Parser parser(lexer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    CHECK(!diagnostics.HasErrors());
    const auto declarations = tree.root->Children();
    const auto inherited = declarations[2]->Children();
    CHECK(inherited[0]->kind == mini_as::NodeKind::Identifier);
    CHECK(inherited[0]->token.lexeme == "Base");
    CHECK(inherited[1]->token.lexeme == "IValue");
}

