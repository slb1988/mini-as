#include "mini_as/tokenizer.hpp"

#include <cctype>
#include <unordered_map>

namespace mini_as {
namespace {

const std::unordered_map<std::string, TokenKind> kKeywords = {
    {"void", TokenKind::KwVoid}, {"bool", TokenKind::KwBool},
    {"int8", TokenKind::KwInt8}, {"int16", TokenKind::KwInt16},
    {"int", TokenKind::KwInt}, {"int32", TokenKind::KwInt},
    {"int64", TokenKind::KwInt64},
    {"uint8", TokenKind::KwUInt8}, {"uint16", TokenKind::KwUInt16},
    {"uint", TokenKind::KwUInt}, {"uint32", TokenKind::KwUInt},
    {"uint64", TokenKind::KwUInt64}, {"float", TokenKind::KwFloat},
    {"double", TokenKind::KwDouble},
    {"string", TokenKind::KwString}, {"true", TokenKind::KwTrue},
    {"false", TokenKind::KwFalse}, {"const", TokenKind::KwConst}, {"auto", TokenKind::KwAuto},
    {"if", TokenKind::KwIf},
    {"else", TokenKind::KwElse}, {"while", TokenKind::KwWhile}, {"do", TokenKind::KwDo},
    {"for", TokenKind::KwFor}, {"switch", TokenKind::KwSwitch}, {"case", TokenKind::KwCase},
    {"default", TokenKind::KwDefault}, {"break", TokenKind::KwBreak},
    {"continue", TokenKind::KwContinue}, {"try", TokenKind::KwTry},
    {"catch", TokenKind::KwCatch},
    {"return", TokenKind::KwReturn}, {"class", TokenKind::KwClass},
    {"interface", TokenKind::KwInterface}, {"enum", TokenKind::KwEnum},
    {"typedef", TokenKind::KwTypedef}, {"funcdef", TokenKind::KwFuncdef},
    {"function", TokenKind::KwFunction},
    {"namespace", TokenKind::KwNamespace},
    {"private", TokenKind::KwPrivate}, {"protected", TokenKind::KwProtected},
    {"cast", TokenKind::KwCast},
    {"in", TokenKind::KwIn}, {"out", TokenKind::KwOut}, {"inout", TokenKind::KwInOut},
    {"is", TokenKind::KwIs},
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
        "end", "identifier", "integer", "bits literal", "float literal", "double literal",
        "string literal", "void", "bool", "int8", "int16", "int", "int64",
        "uint8", "uint16", "uint", "uint64", "float", "double", "string",
        "true", "false", "const", "auto",
        "if", "else", "while", "do", "for", "switch", "case", "default",
        "return", "break", "continue", "try", "catch",
        "class", "interface", "enum", "typedef", "funcdef", "function", "namespace",
        "private", "protected", "cast",
        "in", "out", "inout", "is", "null",
        "(", ")", "{", "}", ",", ".", ";", ":", "::", "?", "@", "+", "-", "*", "**", "/", "%",
        "++", "--",
        "+=", "-=", "*=", "**=", "/=", "%=",
        "&", "|", "^", "~", "<<", ">>", ">>>",
        "&=", "|=", "^=", "<<=", ">>=", ">>>=",
        "!", "!=", "=", "==", "<", "<=", ">", ">=", "&&", "||", "[", "]"
    };
    static_assert(sizeof(names) / sizeof(names[0]) ==
                      static_cast<std::size_t>(TokenKind::RightBracket) + 1,
                  "token name table must match TokenKind");
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
    case '[': Add(TokenKind::LeftBracket, start, location); return;
    case ']': Add(TokenKind::RightBracket, start, location); return;
    case ',': Add(TokenKind::Comma, start, location); return;
    case '.':
        if (std::isdigit(static_cast<unsigned char>(Peek()))) ScanNumber(start, location);
        else Add(TokenKind::Dot, start, location);
        return;
    case ';': Add(TokenKind::Semicolon, start, location); return;
    case ':': Add(Match(':') ? TokenKind::Scope : TokenKind::Colon, start, location); return;
    case '?': Add(TokenKind::Question, start, location); return;
    case '@': Add(TokenKind::At, start, location); return;
    case '+':
        Add(Match('+') ? TokenKind::PlusPlus : (Match('=') ? TokenKind::PlusEqual : TokenKind::Plus),
            start, location); return;
    case '-':
        Add(Match('-') ? TokenKind::MinusMinus : (Match('=') ? TokenKind::MinusEqual : TokenKind::Minus),
            start, location); return;
    case '*':
        if (Match('*')) Add(Match('=') ? TokenKind::StarStarEqual : TokenKind::StarStar, start, location);
        else Add(Match('=') ? TokenKind::StarEqual : TokenKind::Star, start, location);
        return;
    case '%': Add(Match('=') ? TokenKind::PercentEqual : TokenKind::Percent, start, location); return;
    case '!': Add(Match('=') ? TokenKind::BangEqual : TokenKind::Bang, start, location); return;
    case '=': Add(Match('=') ? TokenKind::EqualEqual : TokenKind::Equal, start, location); return;
    case '<':
        if (Match('<')) Add(Match('=') ? TokenKind::ShiftLeftEqual : TokenKind::ShiftLeft, start, location);
        else Add(Match('=') ? TokenKind::LessEqual : TokenKind::Less, start, location);
        return;
    case '>':
        if (Match('>')) {
            if (Match('>')) Add(Match('=') ? TokenKind::ShiftRightArithmeticEqual
                                          : TokenKind::ShiftRightArithmetic, start, location);
            else Add(Match('=') ? TokenKind::ShiftRightEqual : TokenKind::ShiftRight, start, location);
        } else Add(Match('=') ? TokenKind::GreaterEqual : TokenKind::Greater, start, location);
        return;
    case '&':
        if (Match('&')) Add(TokenKind::AndAnd, start, location);
        else Add(Match('=') ? TokenKind::AmpEqual : TokenKind::Amp, start, location);
        return;
    case '|':
        if (Match('|')) Add(TokenKind::OrOr, start, location);
        else Add(Match('=') ? TokenKind::PipeEqual : TokenKind::Pipe, start, location);
        return;
    case '^': Add(Match('=') ? TokenKind::CaretEqual : TokenKind::Caret, start, location); return;
    case '~': Add(TokenKind::Tilde, start, location); return;
    case '/':
        if (Match('/')) { while (!AtEnd() && Peek() != '\n') Advance(); return; }
        if (Match('*')) { SkipBlockComment(location); return; }
        Add(Match('=') ? TokenKind::SlashEqual : TokenKind::Slash, start, location); return;
    case '"': ScanString(start, location); return;
    default:
        if (std::isdigit(static_cast<unsigned char>(ch))) ScanNumber(start, location);
        else if (IsIdentifierStart(ch)) ScanIdentifier(start, location);
        else diagnostics_.Report(location, Severity::Error, "unexpected character");
    }
}

void Tokenizer::ScanNumber(std::size_t start, SourceLocation location) {
    if (source_[start] == '0' &&
        (Peek() == 'b' || Peek() == 'B' || Peek() == 'o' || Peek() == 'O' ||
         Peek() == 'd' || Peek() == 'D' || Peek() == 'x' || Peek() == 'X')) {
        const char prefix = Advance();
        const auto isDigit = [prefix](char ch) {
            if (prefix == 'b' || prefix == 'B') return ch == '0' || ch == '1';
            if (prefix == 'o' || prefix == 'O') return ch >= '0' && ch <= '7';
            if (prefix == 'd' || prefix == 'D') return ch >= '0' && ch <= '9';
            return std::isdigit(static_cast<unsigned char>(ch)) ||
                   (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        };
        const std::size_t digits = current_;
        while (isDigit(Peek())) Advance();
        if (current_ == digits)
            diagnostics_.Report(location, Severity::Error, "numeric base prefix requires digits");
        Add(TokenKind::Bits, start, std::move(location));
        return;
    }

    while (std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
    TokenKind kind = TokenKind::Integer;
    if (source_[start] == '.' || Peek() == '.' || Peek() == 'e' || Peek() == 'E') {
        kind = TokenKind::Double;
        if (source_[start] != '.' && Peek() == '.') Advance();
        while (std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
        if (Peek() == 'e' || Peek() == 'E') {
            Advance();
            if (Peek() == '+' || Peek() == '-') Advance();
            const std::size_t exponent = current_;
            while (std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
            if (current_ == exponent)
                diagnostics_.Report(location, Severity::Error, "floating exponent requires digits");
        }
        if (Peek() == 'f' || Peek() == 'F') {
            kind = TokenKind::Float;
            Advance();
        }
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

