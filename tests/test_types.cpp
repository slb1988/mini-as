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

TEST_CASE(type_checker_exposes_funcdef_signature_metadata) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("funcdef-types",
        "namespace Events { funcdef bool Filter(int, int &inout value); }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    CHECK(checker.Funcdefs().size() == 1);
    const auto& funcdef = checker.Funcdefs()[0];
    CHECK(funcdef.name == "Events::Filter");
    CHECK(funcdef.signature.returnType == mini_as::DataType::Bool());
    CHECK(funcdef.signature.parameters.size() == 2);
    CHECK(funcdef.signature.parameterModes[1] == mini_as::ParameterMode::InOut);
}

TEST_CASE(type_checker_resolves_function_addresses_against_funcdef_signatures) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("function-handle-types",
        "funcdef int Unary(int); int identity(int value) { return value; } "
        "int run() { Unary@ callback = @identity; return callback(42); }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    const auto variable = tree.root->Children()[2]->firstChild->firstChild;
    CHECK(variable->declaredType == mini_as::DataType::Function("Unary", true));
    CHECK(variable->firstChild->inferredType == mini_as::DataType::Function("Unary", true));
}

TEST_CASE(type_checker_resolves_delegate_construction_to_an_instance_method) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("delegate-types",
        "funcdef int Unary(int); class Target { int apply(int value) { return value; } } "
        "int run() { Target@ object = Target(); Unary@ callback = Unary(object.apply); "
        "return callback(42); }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    const auto body = tree.root->Children()[2]->firstChild;
    const auto callback = body->firstChild->nextSibling;
    CHECK(callback->firstChild->inferredType == mini_as::DataType::Function("Unary", true));
    CHECK(callback->firstChild->delegateObjectType == "Target");
    CHECK(callback->firstChild->operatorMethod == "int apply(int)");
}

TEST_CASE(type_checker_infers_anonymous_function_types_and_records_captures) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("lambda-types",
        "funcdef int Step(int); int run() { int total = 1; Step@ callback = "
        "function(value) { total += value; return total; }; return callback(1); }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    auto* lambda = tree.root->Children()[1]->firstChild->firstChild->nextSibling->firstChild;
    CHECK(lambda->inferredType == mini_as::DataType::Function("Step", true));
    CHECK(lambda->firstChild->declaredType == mini_as::DataType::Int());
    CHECK(lambda->captureNames.size() == 1);
    CHECK(lambda->captureNames[0] == "total");
}

TEST_CASE(type_checker_publishes_child_funcdef_parent_metadata) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("child-funcdef-types",
        "class First { funcdef int Callback(int); Callback@ callback; } "
        "class Second { funcdef int Callback(int); Callback@ callback; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    CHECK(checker.Funcdefs().size() == 2);
    CHECK(checker.Funcdefs()[0].name == "First::Callback");
    CHECK(checker.Funcdefs()[0].parentType == "First");
    CHECK(checker.Funcdefs()[1].name == "Second::Callback");
    CHECK(checker.Funcdefs()[1].parentType == "Second");
    CHECK(checker.Funcdefs()[0].signature.parameters[0] == mini_as::DataType::Int());
}

TEST_CASE(type_checker_resolves_weakref_construction_get_and_implicit_lock) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("weakref-types",
        "class Payload { int value; } int run() { Payload@ object = Payload(); "
        "weakref<Payload> reference(object); Payload@ first = reference.get(); "
        "Payload@ second = reference; return first is second ? 42 : 0; }", diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    auto* statement = tree.root->Children()[1]->firstChild->firstChild->nextSibling;
    CHECK(statement->declaredType == mini_as::DataType::WeakRef("Payload"));
    statement = statement->nextSibling;
    CHECK(statement->firstChild->inferredType == mini_as::DataType::Object("Payload", true));
    statement = statement->nextSibling;
    CHECK(statement->firstChild->inferredType == mini_as::DataType::WeakRef("Payload"));
}

TEST_CASE(type_checker_resolves_current_parent_and_explicit_namespaces) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("types",
        "namespace Root { int base = 2; namespace Left { int value = 40; "
        "int read() { return value + base; } } namespace Right { int value = 1; } } "
        "int main() { return Root::Left::read() + Root::Right::value - 1; }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    CHECK(checker.Globals().size() == 3);
    CHECK(checker.Globals()[0].name == "Root::base");
    CHECK(checker.Globals()[1].name == "Root::Left::value");
    CHECK(checker.Functions().back().name == "main");
}

TEST_CASE(type_checker_counts_and_validates_default_arguments) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("types",
        "int add(int value, int first = 1, int second = 2) { return value + first + second; } "
        "int main() { return add(39); }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    CHECK(checker.Functions()[0].defaultArgumentCount == 2);
}

TEST_CASE(type_checker_matches_named_arguments_by_parameter_name) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer tokenizer("types",
        "int combine(int first, int second = 0, int third = 0) { return first + second + third; } "
        "int main() { return combine(third: 2, first: 40); }",
        diagnostics);
    mini_as::Parser parser(tokenizer.ScanAll(), diagnostics);
    auto tree = parser.Parse();
    mini_as::TypeChecker checker(diagnostics);
    CHECK(checker.Check(tree.root));
    CHECK(checker.Functions()[0].parameterNames[2] == "third");
}

