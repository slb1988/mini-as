#include "mini_as/parser.hpp"

#include <utility>

namespace mini_as {

void AstNode::AppendChild(AstNode* child) {
    if (!child) return;
    if (!firstChild) { firstChild = child; return; }
    AstNode* tail = firstChild;
    while (tail->nextSibling) tail = tail->nextSibling;
    tail->nextSibling = child;
}

std::vector<AstNode*> AstNode::Children() const {
    std::vector<AstNode*> result;
    for (AstNode* child = firstChild; child; child = child->nextSibling) result.push_back(child);
    return result;
}

AstNode* AstArena::Make(NodeKind kind, const Token& token) {
    auto node = std::make_unique<AstNode>();
    node->kind = kind;
    node->token = token;
    AstNode* result = node.get();
    nodes_.push_back(std::move(node));
    return result;
}

Parser::Parser(std::vector<Token> tokens, DiagnosticSink& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

SyntaxTree Parser::Parse() {
    SyntaxTree tree;
    arena_ = &tree.arena;
    tree.root = arena_->Make(NodeKind::Program, Current());
    while (!Check(TokenKind::End)) {
        const auto before = current_;
        tree.root->AppendChild(ParseTopLevel());
        if (current_ == before) { Error(Current(), "parser made no progress"); Advance(); }
    }
    arena_ = nullptr;
    return tree;
}

AstNode* Parser::ParseTopLevel() {
    if (Match(TokenKind::KwClass)) return ParseClass(false);
    if (Match(TokenKind::KwInterface)) return ParseClass(true);
    if (IsTypeStart()) {
        const auto saved = current_;
        DataType type = ParseType(true);
        if (Check(TokenKind::Identifier)) {
            Token name = Advance();
            if (Check(TokenKind::LeftParen)) return ParseFunction(type, std::move(name));
        }
        current_ = saved;
    }
    AstNode* statement = ParseStatement();
    if (statement && statement->kind == NodeKind::VarDecl) statement->isGlobal = true;
    if (statement && statement->kind == NodeKind::DeclList) {
        for (AstNode* declaration = statement->firstChild; declaration;
             declaration = declaration->nextSibling) declaration->isGlobal = true;
    }
    return statement;
}

AstNode* Parser::ParseClass(bool isInterface) {
    Token name = Consume(TokenKind::Identifier, "expected type name");
    AstNode* node = arena_->Make(isInterface ? NodeKind::InterfaceDecl : NodeKind::ClassDecl, name);
    if (Match(TokenKind::Colon)) {
        node->AppendChild(arena_->Make(NodeKind::Identifier,
                         Consume(TokenKind::Identifier, "expected interface name")));
    }
    Consume(TokenKind::LeftBrace, "expected '{' before type body");
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
        DataType type = ParseType(true);
        Token memberName = Consume(TokenKind::Identifier, "expected member name");
        if (Check(TokenKind::LeftParen)) {
            AstNode* method = ParseFunction(type, std::move(memberName));
            node->AppendChild(method);
        } else {
            AstNode* field = arena_->Make(NodeKind::FieldDecl, memberName);
            field->declaredType = type;
            Consume(TokenKind::Semicolon, "expected ';' after field");
            node->AppendChild(field);
        }
    }
    Consume(TokenKind::RightBrace, "expected '}' after type body");
    Match(TokenKind::Semicolon);
    return node;
}

AstNode* Parser::ParseFunction(DataType returnType, Token name) {
    AstNode* function = arena_->Make(NodeKind::FunctionDecl, name);
    function->declaredType = std::move(returnType);
    Consume(TokenKind::LeftParen, "expected '(' after function name");
    if (!Check(TokenKind::RightParen)) {
        do {
            DataType type = ParseType(false);
            Token paramName = Consume(TokenKind::Identifier, "expected parameter name");
            AstNode* parameter = arena_->Make(NodeKind::Parameter, paramName);
            parameter->declaredType = std::move(type);
            function->AppendChild(parameter);
        } while (Match(TokenKind::Comma));
    }
    Consume(TokenKind::RightParen, "expected ')' after parameters");
    if (Match(TokenKind::Semicolon)) return function;
    function->AppendChild(ParseBlock());
    return function;
}

AstNode* Parser::ParseBlock() {
    Token brace = Consume(TokenKind::LeftBrace, "expected '{'");
    AstNode* block = arena_->Make(NodeKind::Block, brace);
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
        const auto before = current_;
        block->AppendChild(ParseStatement());
        if (current_ == before) { Synchronize(); if (current_ == before) Advance(); }
    }
    Consume(TokenKind::RightBrace, "expected '}' after block");
    return block;
}

AstNode* Parser::ParseStatement() {
    if (Check(TokenKind::LeftBrace)) return ParseBlock();
    if (Match(TokenKind::KwIf)) return ParseIf();
    if (Match(TokenKind::KwWhile)) return ParseWhile();
    if (Match(TokenKind::KwDo)) return ParseDoWhile();
    if (Match(TokenKind::KwFor)) return ParseFor();
    if (Match(TokenKind::KwSwitch)) return ParseSwitch();
    if (Match(TokenKind::KwReturn)) return ParseReturn();
    if (IsVariableDeclarationStart()) return ParseVariableDeclaration();
    AstNode* statement = arena_->Make(NodeKind::ExprStmt, Current());
    statement->AppendChild(ParseExpression());
    Consume(TokenKind::Semicolon, "expected ';' after expression");
    return statement;
}

AstNode* Parser::ParseVariableDeclaration() {
    const bool isConst = Match(TokenKind::KwConst);
    const bool isAuto = Match(TokenKind::KwAuto);
    DataType type = isAuto ? DataType::Invalid() : ParseType(false);
    auto parseOne = [&]() {
        Token name = Consume(TokenKind::Identifier, "expected variable name");
        AstNode* declaration = arena_->Make(NodeKind::VarDecl, name);
        declaration->declaredType = type;
        declaration->isConst = isConst;
        declaration->isAuto = isAuto;
        if (Match(TokenKind::Equal)) declaration->AppendChild(ParseAssignment());
        return declaration;
    };
    AstNode* first = parseOne();
    if (!Match(TokenKind::Comma)) {
        Consume(TokenKind::Semicolon, "expected ';' after declaration");
        return first;
    }
    AstNode* declarations = arena_->Make(NodeKind::DeclList, first->token);
    declarations->AppendChild(first);
    do { declarations->AppendChild(parseOne()); } while (Match(TokenKind::Comma));
    Consume(TokenKind::Semicolon, "expected ';' after declaration");
    return declarations;
}

AstNode* Parser::ParseIf() {
    AstNode* node = arena_->Make(NodeKind::IfStmt, Previous());
    Consume(TokenKind::LeftParen, "expected '(' after if");
    node->AppendChild(ParseExpression());
    Consume(TokenKind::RightParen, "expected ')' after condition");
    node->AppendChild(ParseStatement());
    if (Match(TokenKind::KwElse)) node->AppendChild(ParseStatement());
    return node;
}

AstNode* Parser::ParseWhile() {
    AstNode* node = arena_->Make(NodeKind::WhileStmt, Previous());
    Consume(TokenKind::LeftParen, "expected '(' after while");
    node->AppendChild(ParseExpression());
    Consume(TokenKind::RightParen, "expected ')' after condition");
    node->AppendChild(ParseStatement());
    return node;
}

AstNode* Parser::ParseDoWhile() {
    AstNode* node = arena_->Make(NodeKind::DoWhileStmt, Previous());
    node->AppendChild(ParseStatement());
    Consume(TokenKind::KwWhile, "expected 'while' after do body");
    Consume(TokenKind::LeftParen, "expected '(' after while");
    node->AppendChild(ParseExpression());
    Consume(TokenKind::RightParen, "expected ')' after do-while condition");
    Consume(TokenKind::Semicolon, "expected ';' after do-while");
    return node;
}

AstNode* Parser::ParseFor() {
    AstNode* node = arena_->Make(NodeKind::ForStmt, Previous());
    Consume(TokenKind::LeftParen, "expected '(' after for");
    if (Match(TokenKind::Semicolon)) node->AppendChild(arena_->Make(NodeKind::EmptyStmt, Previous()));
    else if (IsVariableDeclarationStart()) node->AppendChild(ParseVariableDeclaration());
    else {
        AstNode* initializer = arena_->Make(NodeKind::ExprStmt, Current());
        initializer->AppendChild(ParseExpression());
        Consume(TokenKind::Semicolon, "expected ';' after for initializer");
        node->AppendChild(initializer);
    }
    if (Check(TokenKind::Semicolon)) node->AppendChild(arena_->Make(NodeKind::EmptyStmt, Current()));
    else node->AppendChild(ParseExpression());
    Consume(TokenKind::Semicolon, "expected ';' after for condition");
    if (Check(TokenKind::RightParen)) node->AppendChild(arena_->Make(NodeKind::EmptyStmt, Current()));
    else node->AppendChild(ParseExpression());
    Consume(TokenKind::RightParen, "expected ')' after for clauses");
    node->AppendChild(ParseStatement());
    return node;
}

AstNode* Parser::ParseSwitch() {
    AstNode* node = arena_->Make(NodeKind::SwitchStmt, Previous());
    Consume(TokenKind::LeftParen, "expected '(' after switch");
    node->AppendChild(ParseExpression());
    Consume(TokenKind::RightParen, "expected ')' after switch expression");
    Consume(TokenKind::LeftBrace, "expected '{' before switch body");
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
        AstNode* clause = nullptr;
        if (Match(TokenKind::KwCase)) {
            clause = arena_->Make(NodeKind::CaseClause, Previous());
            clause->AppendChild(ParseExpression());
            Consume(TokenKind::Colon, "expected ':' after case value");
        } else if (Match(TokenKind::KwDefault)) {
            clause = arena_->Make(NodeKind::DefaultClause, Previous());
            Consume(TokenKind::Colon, "expected ':' after default");
        } else {
            Error(Current(), "expected case or default in switch");
            Synchronize();
            if (!Check(TokenKind::KwCase) && !Check(TokenKind::KwDefault) &&
                !Check(TokenKind::RightBrace) && !Check(TokenKind::End)) Advance();
            continue;
        }
        while (!Check(TokenKind::KwCase) && !Check(TokenKind::KwDefault) &&
               !Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
            clause->AppendChild(ParseStatement());
        }
        node->AppendChild(clause);
    }
    Consume(TokenKind::RightBrace, "expected '}' after switch body");
    return node;
}

AstNode* Parser::ParseReturn() {
    AstNode* node = arena_->Make(NodeKind::ReturnStmt, Previous());
    if (!Check(TokenKind::Semicolon)) node->AppendChild(ParseExpression());
    Consume(TokenKind::Semicolon, "expected ';' after return");
    return node;
}

AstNode* Parser::ParseExpression() { return ParseAssignment(); }

AstNode* Parser::ParseAssignment() {
    AstNode* left = ParseOr();
    if (!Match(TokenKind::Equal)) return left;
    AstNode* node = arena_->Make(NodeKind::Assign, Previous());
    node->AppendChild(left);
    node->AppendChild(ParseAssignment());
    return node;
}

#define BINARY_LEVEL(method, next, ...) \
AstNode* Parser::method() { \
    AstNode* expression = next(); \
    while (MatchAny({__VA_ARGS__})) { \
        AstNode* node = arena_->Make(NodeKind::Binary, Previous()); \
        node->AppendChild(expression); node->AppendChild(next()); expression = node; \
    } \
    return expression; \
}

BINARY_LEVEL(ParseOr, ParseAnd, TokenKind::OrOr)
BINARY_LEVEL(ParseAnd, ParseEquality, TokenKind::AndAnd)
BINARY_LEVEL(ParseEquality, ParseComparison, TokenKind::EqualEqual, TokenKind::BangEqual, TokenKind::KwIs)
BINARY_LEVEL(ParseComparison, ParseTerm, TokenKind::Less, TokenKind::LessEqual, TokenKind::Greater, TokenKind::GreaterEqual)
BINARY_LEVEL(ParseTerm, ParseFactor, TokenKind::Plus, TokenKind::Minus)
BINARY_LEVEL(ParseFactor, ParseUnary, TokenKind::Star, TokenKind::Slash, TokenKind::Percent)
#undef BINARY_LEVEL

AstNode* Parser::ParseUnary() {
    if (MatchAny({TokenKind::Bang, TokenKind::Minus, TokenKind::Plus, TokenKind::At})) {
        AstNode* node = arena_->Make(NodeKind::Unary, Previous());
        node->AppendChild(ParseUnary());
        return node;
    }
    return ParseCall();
}

AstNode* Parser::ParseCall() {
    AstNode* expression = ParsePrimary();
    for (;;) {
        if (Match(TokenKind::LeftParen)) {
            AstNode* call = arena_->Make(NodeKind::Call, Previous());
            call->AppendChild(expression);
            if (!Check(TokenKind::RightParen)) {
                do { call->AppendChild(ParseExpression()); } while (Match(TokenKind::Comma));
            }
            Consume(TokenKind::RightParen, "expected ')' after arguments");
            expression = call;
        } else if (Match(TokenKind::Dot)) {
            AstNode* member = arena_->Make(NodeKind::Member,
                            Consume(TokenKind::Identifier, "expected member name"));
            member->AppendChild(expression);
            expression = member;
        } else break;
    }
    return expression;
}

AstNode* Parser::ParsePrimary() {
    if (MatchAny({TokenKind::Integer, TokenKind::Float, TokenKind::String,
                  TokenKind::KwTrue, TokenKind::KwFalse, TokenKind::KwNull})) {
        return arena_->Make(NodeKind::Literal, Previous());
    }
    if (Match(TokenKind::Identifier)) return arena_->Make(NodeKind::Identifier, Previous());
    if (Match(TokenKind::LeftParen)) {
        AstNode* expression = ParseExpression();
        Consume(TokenKind::RightParen, "expected ')' after expression");
        return expression;
    }
    Error(Current(), "expected expression");
    Token bad = Advance();
    return arena_->Make(NodeKind::Literal, bad);
}

DataType Parser::ParseType(bool allowVoid) {
    DataType type;
    if (allowVoid && Match(TokenKind::KwVoid)) type = DataType::Void();
    else if (Match(TokenKind::KwBool)) type = DataType::Bool();
    else if (Match(TokenKind::KwInt)) type = DataType::Int();
    else if (Match(TokenKind::KwFloat)) type = DataType::Float();
    else if (Match(TokenKind::KwString)) type = DataType::String();
    else if (Match(TokenKind::Identifier)) type = DataType::Object(Previous().lexeme);
    else { Error(Current(), "expected type"); return DataType::Invalid(); }
    if (Match(TokenKind::At)) type.isHandle = true;
    return type;
}

bool Parser::IsTypeStart(bool allowIdentifier) const {
    switch (Current().kind) {
    case TokenKind::KwVoid: case TokenKind::KwBool: case TokenKind::KwInt:
    case TokenKind::KwFloat: case TokenKind::KwString: return true;
    case TokenKind::Identifier: return allowIdentifier;
    default: return false;
    }
}

bool Parser::IsVariableDeclarationStart() const {
    if (Check(TokenKind::KwConst) || Check(TokenKind::KwAuto)) return true;
    if (!IsTypeStart()) return false;
    return Current().kind != TokenKind::Identifier ||
        (current_ + 1 < tokens_.size() &&
         (tokens_[current_ + 1].kind == TokenKind::Identifier ||
          tokens_[current_ + 1].kind == TokenKind::At));
}

bool Parser::Match(TokenKind kind) { if (!Check(kind)) return false; Advance(); return true; }
bool Parser::MatchAny(std::initializer_list<TokenKind> kinds) {
    for (const auto kind : kinds) if (Match(kind)) return true;
    return false;
}
bool Parser::Check(TokenKind kind) const { return Current().kind == kind; }
const Token& Parser::Advance() { if (!Check(TokenKind::End)) ++current_; return Previous(); }
const Token& Parser::Current() const { return tokens_[current_]; }
const Token& Parser::Previous() const { return tokens_[current_ ? current_ - 1 : 0]; }

Token Parser::Consume(TokenKind kind, const char* message) {
    if (Check(kind)) return Advance();
    Error(Current(), message);
    return Current();
}

void Parser::Error(const Token& token, std::string message) {
    diagnostics_.Report(token.location, Severity::Error, std::move(message));
}

void Parser::Synchronize() {
    while (!Check(TokenKind::End)) {
        if (current_ && Previous().kind == TokenKind::Semicolon) return;
        switch (Current().kind) {
        case TokenKind::KwIf: case TokenKind::KwWhile: case TokenKind::KwDo:
        case TokenKind::KwFor: case TokenKind::KwSwitch: case TokenKind::KwCase:
        case TokenKind::KwDefault: case TokenKind::KwReturn:
        case TokenKind::KwClass: case TokenKind::KwInterface: return;
        default: Advance();
        }
    }
}

} // namespace mini_as

