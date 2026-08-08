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

