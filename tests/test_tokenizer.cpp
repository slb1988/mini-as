#include "test.hpp"
#include "mini_as/tokenizer.hpp"

TEST_CASE(tokenizer_tracks_tokens_and_locations) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("lex", "int answer = 40 + 2; // ok\n\"x\\n\" != \"y\"", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens.size() == 11);
    CHECK(tokens[0].kind == mini_as::TokenKind::KwInt);
    CHECK(tokens[1].lexeme == "answer");
    CHECK(tokens[7].location.row == 2);
}

TEST_CASE(tokenizer_reports_unterminated_input) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("bad", "/* never closed", diagnostics);
    lexer.ScanAll();
    CHECK(diagnostics.HasErrors());
    CHECK(diagnostics.All()[0].location.row == 1);
}

TEST_CASE(tokenizer_recognizes_const_qualifier) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("const", "const int answer = 42;", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::KwConst);
    CHECK(tokens[1].kind == mini_as::TokenKind::KwInt);
}

TEST_CASE(tokenizer_recognizes_member_access_qualifiers) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("access", "private protected", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::KwPrivate);
    CHECK(tokens[1].kind == mini_as::TokenKind::KwProtected);
}

TEST_CASE(tokenizer_recognizes_reference_cast_keyword) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("cast", "cast<Derived>(value)", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::KwCast);
    CHECK(tokens[1].kind == mini_as::TokenKind::Less);
    CHECK(tokens[3].kind == mini_as::TokenKind::Greater);
}

TEST_CASE(tokenizer_recognizes_the_integer_type_family) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("integers",
        "int8 int16 int int32 int64 uint8 uint16 uint uint32 uint64", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::KwInt8);
    CHECK(tokens[1].kind == mini_as::TokenKind::KwInt16);
    CHECK(tokens[2].kind == mini_as::TokenKind::KwInt);
    CHECK(tokens[3].kind == mini_as::TokenKind::KwInt);
    CHECK(tokens[4].kind == mini_as::TokenKind::KwInt64);
    CHECK(tokens[5].kind == mini_as::TokenKind::KwUInt8);
    CHECK(tokens[6].kind == mini_as::TokenKind::KwUInt16);
    CHECK(tokens[7].kind == mini_as::TokenKind::KwUInt);
    CHECK(tokens[8].kind == mini_as::TokenKind::KwUInt);
    CHECK(tokens[9].kind == mini_as::TokenKind::KwUInt64);
}

TEST_CASE(tokenizer_recognizes_numeric_bases_exponents_and_float_suffixes) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("numbers",
        "0b1010 0o12 0d10 0xA 2147483648 1.5 1.5f .5 1e2", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::Bits);
    CHECK(tokens[1].kind == mini_as::TokenKind::Bits);
    CHECK(tokens[2].kind == mini_as::TokenKind::Bits);
    CHECK(tokens[3].kind == mini_as::TokenKind::Bits);
    CHECK(tokens[4].kind == mini_as::TokenKind::Integer);
    CHECK(tokens[5].kind == mini_as::TokenKind::Double);
    CHECK(tokens[6].kind == mini_as::TokenKind::Float);
    CHECK(tokens[7].kind == mini_as::TokenKind::Double);
    CHECK(tokens[8].kind == mini_as::TokenKind::Double);
}

TEST_CASE(tokenizer_recognizes_bitwise_shift_and_assignment_tokens) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("bits", "~a & b | c ^ d << 1 >> 2 >>> 3 &= 4 |= 5 ^= 6 <<= 7 >>= 8 >>>= 9",
                             diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::Tilde);
    CHECK(tokens[2].kind == mini_as::TokenKind::Amp);
    CHECK(tokens[4].kind == mini_as::TokenKind::Pipe);
    CHECK(tokens[6].kind == mini_as::TokenKind::Caret);
    CHECK(tokens[8].kind == mini_as::TokenKind::ShiftLeft);
    CHECK(tokens[10].kind == mini_as::TokenKind::ShiftRight);
    CHECK(tokens[12].kind == mini_as::TokenKind::ShiftRightArithmetic);
    CHECK(tokens[14].kind == mini_as::TokenKind::AmpEqual);
    CHECK(tokens[16].kind == mini_as::TokenKind::PipeEqual);
    CHECK(tokens[18].kind == mini_as::TokenKind::CaretEqual);
    CHECK(tokens[20].kind == mini_as::TokenKind::ShiftLeftEqual);
    CHECK(tokens[22].kind == mini_as::TokenKind::ShiftRightEqual);
    CHECK(tokens[24].kind == mini_as::TokenKind::ShiftRightArithmeticEqual);
}

TEST_CASE(tokenizer_recognizes_exponent_operators) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("power", "2 ** 3; value **= 2;", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[1].kind == mini_as::TokenKind::StarStar);
    CHECK(tokens[5].kind == mini_as::TokenKind::StarStarEqual);
}

TEST_CASE(tokenizer_recognizes_enum_declarations) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("enum", "enum Color { Red, Green = 3 }", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::KwEnum);
    CHECK(tokens[1].kind == mini_as::TokenKind::Identifier);
    CHECK(tokens[4].kind == mini_as::TokenKind::Comma);
}

TEST_CASE(tokenizer_recognizes_typedef_declarations) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("typedef", "typedef uint64 EntityId;", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::KwTypedef);
    CHECK(tokens[1].kind == mini_as::TokenKind::KwUInt64);
    CHECK(tokens[2].lexeme == "EntityId");
}

TEST_CASE(tokenizer_recognizes_namespaces_and_scope_resolution) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("namespace", "namespace Math { int x = Math::answer; }", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::KwNamespace);
    CHECK(tokens[7].kind == mini_as::TokenKind::Scope);
    CHECK(tokens[7].lexeme == "::");
}

TEST_CASE(tokenizer_keeps_property_as_a_contextual_identifier) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("property", "int get_value() const property;", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[5].kind == mini_as::TokenKind::Identifier);
    CHECK(tokens[5].lexeme == "property");
}

TEST_CASE(tokenizer_recognizes_funcdef_declarations) {
    mini_as::DiagnosticSink diagnostics;
    mini_as::Tokenizer lexer("funcdef", "funcdef bool Filter(int, int);", diagnostics);
    const auto tokens = lexer.ScanAll();
    CHECK(!diagnostics.HasErrors());
    CHECK(tokens[0].kind == mini_as::TokenKind::KwFuncdef);
}

