#pragma once

#include "mini_as/core.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mini_as {

enum class TokenKind {
    End, Identifier, Integer, Bits, Float, Double, String,
    KwVoid, KwBool, KwInt8, KwInt16, KwInt, KwInt64,
    KwUInt8, KwUInt16, KwUInt, KwUInt64,
    KwFloat, KwDouble, KwString, KwTrue, KwFalse, KwConst, KwAuto,
    KwIf, KwElse, KwWhile, KwDo, KwFor, KwSwitch, KwCase, KwDefault,
    KwReturn, KwBreak, KwContinue, KwClass, KwInterface, KwEnum, KwTypedef, KwNamespace,
    KwIn, KwOut, KwInOut, KwIs, KwNull,
    LeftParen, RightParen, LeftBrace, RightBrace,
    Comma, Dot, Semicolon, Colon, Scope, Question, At,
    Plus, Minus, Star, StarStar, Slash, Percent,
    PlusPlus, MinusMinus,
    PlusEqual, MinusEqual, StarEqual, StarStarEqual, SlashEqual, PercentEqual,
    Amp, Pipe, Caret, Tilde, ShiftLeft, ShiftRight, ShiftRightArithmetic,
    AmpEqual, PipeEqual, CaretEqual, ShiftLeftEqual, ShiftRightEqual, ShiftRightArithmeticEqual,
    Bang, BangEqual, Equal, EqualEqual,
    Less, LessEqual, Greater, GreaterEqual,
    AndAnd, OrOr
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string lexeme;
    SourceLocation location;
};

std::string_view TokenName(TokenKind kind);

class Tokenizer {
public:
    Tokenizer(std::string section, std::string_view source, DiagnosticSink& diagnostics);
    std::vector<Token> ScanAll();

private:
    bool AtEnd() const;
    char Peek(std::size_t lookahead = 0) const;
    char Advance();
    bool Match(char expected);
    SourceLocation Location() const;
    void Add(TokenKind kind, std::size_t start, SourceLocation location);
    void ScanToken();
    void ScanNumber(std::size_t start, SourceLocation location);
    void ScanIdentifier(std::size_t start, SourceLocation location);
    void ScanString(std::size_t start, SourceLocation location);
    void SkipBlockComment(SourceLocation location);

    std::string section_;
    std::string_view source_;
    DiagnosticSink& diagnostics_;
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    int row_ = 1;
    int column_ = 1;
};

} // namespace mini_as

