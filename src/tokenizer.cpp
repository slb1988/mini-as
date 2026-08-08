#include "mini_as/tokenizer.hpp"

#include <cctype>
#include <unordered_map>

namespace mini_as {
namespace {

const std::unordered_map<std::string, TokenKind> kKeywords = {
    {"void", TokenKind::KwVoid}, {"bool", TokenKind::KwBool},
    {"int", TokenKind::KwInt}, {"float", TokenKind::KwFloat},
    {"string", TokenKind::KwString}, {"true", TokenKind::KwTrue},
    {"false", TokenKind::KwFalse}, {"const", TokenKind::KwConst}, {"auto", TokenKind::KwAuto},
    {"if", TokenKind::KwIf},
    {"else", TokenKind::KwElse}, {"while", TokenKind::KwWhile}, {"do", TokenKind::KwDo},
    {"for", TokenKind::KwFor},
    {"return", TokenKind::KwReturn}, {"class", TokenKind::KwClass},
    {"interface", TokenKind::KwInterface}, {"is", TokenKind::KwIs},
    {"null", TokenKind::KwNull},
};

bool IsIdentifierStart(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}
bool IsIdentifierPart(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

} // namespace

std::string_view TokenName(TokenKind kind) {
    static const char* names[] = {
        "end", "identifier", "integer", "float literal", "string literal",
        "void", "bool", "int", "float", "string", "true", "false", "const", "auto",
        "if", "else", "while", "do", "for", "return", "class", "interface", "is", "null",
        "(", ")", "{", "}", ",", ".", ";", ":", "@", "+", "-", "*", "/", "%",
        "!", "!=", "=", "==", "<", "<=", ">", ">=", "&&", "||"
    };
    return names[static_cast<std::size_t>(kind)];
}

Tokenizer::Tokenizer(std::string section, std::string_view source, DiagnosticSink& diagnostics)
    : section_(std::move(section)), source_(source), diagnostics_(diagnostics) {}

std::vector<Token> Tokenizer::ScanAll() {
    while (!AtEnd()) ScanToken();
    tokens_.push_back({TokenKind::End, {}, Location()});
    return tokens_;
}

bool Tokenizer::AtEnd() const { return current_ >= source_.size(); }
char Tokenizer::Peek(std::size_t lookahead) const {
    const auto index = current_ + lookahead;
    return index < source_.size() ? source_[index] : '\0';
}

char Tokenizer::Advance() {
    const char ch = source_[current_++];
    if (ch == '\n') { ++row_; column_ = 1; }
    else { ++column_; }
    return ch;
}

bool Tokenizer::Match(char expected) {
    if (Peek() != expected) return false;
    Advance();
    return true;
}

SourceLocation Tokenizer::Location() const { return {section_, current_, row_, column_}; }

void Tokenizer::Add(TokenKind kind, std::size_t start, SourceLocation location) {
    tokens_.push_back({kind, std::string(source_.substr(start, current_ - start)), std::move(location)});
}

void Tokenizer::ScanToken() {
    const std::size_t start = current_;
    SourceLocation location = Location();
    const char ch = Advance();
    switch (ch) {
    case ' ': case '\r': case '\t': return;
    case '\n': return;
    case '(': Add(TokenKind::LeftParen, start, location); return;
    case ')': Add(TokenKind::RightParen, start, location); return;
    case '{': Add(TokenKind::LeftBrace, start, location); return;
    case '}': Add(TokenKind::RightBrace, start, location); return;
    case ',': Add(TokenKind::Comma, start, location); return;
    case '.': Add(TokenKind::Dot, start, location); return;
    case ';': Add(TokenKind::Semicolon, start, location); return;
    case ':': Add(TokenKind::Colon, start, location); return;
    case '@': Add(TokenKind::At, start, location); return;
    case '+': Add(TokenKind::Plus, start, location); return;
    case '-': Add(TokenKind::Minus, start, location); return;
    case '*': Add(TokenKind::Star, start, location); return;
    case '%': Add(TokenKind::Percent, start, location); return;
    case '!': Add(Match('=') ? TokenKind::BangEqual : TokenKind::Bang, start, location); return;
    case '=': Add(Match('=') ? TokenKind::EqualEqual : TokenKind::Equal, start, location); return;
    case '<': Add(Match('=') ? TokenKind::LessEqual : TokenKind::Less, start, location); return;
    case '>': Add(Match('=') ? TokenKind::GreaterEqual : TokenKind::Greater, start, location); return;
    case '&':
        if (Match('&')) Add(TokenKind::AndAnd, start, location);
        else diagnostics_.Report(location, Severity::Error, "expected '&' after '&'");
        return;
    case '|':
        if (Match('|')) Add(TokenKind::OrOr, start, location);
        else diagnostics_.Report(location, Severity::Error, "expected '|' after '|'");
        return;
    case '/':
        if (Match('/')) { while (!AtEnd() && Peek() != '\n') Advance(); return; }
        if (Match('*')) { SkipBlockComment(location); return; }
        Add(TokenKind::Slash, start, location); return;
    case '"': ScanString(start, location); return;
    default:
        if (std::isdigit(static_cast<unsigned char>(ch))) ScanNumber(start, location);
        else if (IsIdentifierStart(ch)) ScanIdentifier(start, location);
        else diagnostics_.Report(location, Severity::Error, "unexpected character");
    }
}

void Tokenizer::ScanNumber(std::size_t start, SourceLocation location) {
    while (std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
    TokenKind kind = TokenKind::Integer;
    if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(Peek(1)))) {
        kind = TokenKind::Float;
        Advance();
        while (std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
    }
    Add(kind, start, std::move(location));
}

void Tokenizer::ScanIdentifier(std::size_t start, SourceLocation location) {
    while (IsIdentifierPart(Peek())) Advance();
    const std::string text(source_.substr(start, current_ - start));
    const auto found = kKeywords.find(text);
    tokens_.push_back({found == kKeywords.end() ? TokenKind::Identifier : found->second,
                       text, std::move(location)});
}

void Tokenizer::ScanString(std::size_t start, SourceLocation location) {
    bool escaped = false;
    while (!AtEnd()) {
        const char ch = Advance();
        if (!escaped && ch == '"') { Add(TokenKind::String, start, std::move(location)); return; }
        if (!escaped && ch == '\n') {
            diagnostics_.Report(location, Severity::Error, "unterminated string literal");
            return;
        }
        if (!escaped && ch == '\\') escaped = true;
        else escaped = false;
    }
    diagnostics_.Report(location, Severity::Error, "unterminated string literal");
}

void Tokenizer::SkipBlockComment(SourceLocation location) {
    while (!AtEnd()) {
        if (Peek() == '*' && Peek(1) == '/') { Advance(); Advance(); return; }
        Advance();
    }
    diagnostics_.Report(std::move(location), Severity::Error, "unterminated block comment");
}

} // namespace mini_as

