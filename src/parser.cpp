#include "mini_as/parser.hpp"

#include <optional>
#include <utility>

namespace mini_as {
namespace {

std::optional<DataType> TypedefPrimitive(TokenKind kind) {
    switch (kind) {
    case TokenKind::KwBool: return DataType::Bool();
    case TokenKind::KwInt8: return DataType::Int8();
    case TokenKind::KwInt16: return DataType::Int16();
    case TokenKind::KwInt: return DataType::Int();
    case TokenKind::KwInt64: return DataType::Int64();
    case TokenKind::KwUInt8: return DataType::UInt8();
    case TokenKind::KwUInt16: return DataType::UInt16();
    case TokenKind::KwUInt: return DataType::UInt();
    case TokenKind::KwUInt64: return DataType::UInt64();
    case TokenKind::KwFloat: return DataType::Float();
    case TokenKind::KwDouble: return DataType::Double();
    default: return std::nullopt;
    }
}

std::string JoinName(std::string_view nameSpace, std::string_view name) {
    if (nameSpace.empty()) return std::string(name);
    return std::string(nameSpace) + "::" + std::string(name);
}

} // namespace

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
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {
    struct NamespaceFrame { std::string name; int depth = 0; };
    std::vector<NamespaceFrame> namespaces;
    int braceDepth = 0;
    for (std::size_t index = 0; index < tokens_.size(); ++index) {
        if (tokens_[index].kind == TokenKind::RightBrace) {
            if (!namespaces.empty() && namespaces.back().depth == braceDepth) namespaces.pop_back();
            --braceDepth;
            continue;
        }
        if (tokens_[index].kind == TokenKind::LeftBrace) { ++braceDepth; continue; }
        const std::string active = namespaces.empty() ? std::string{} : namespaces.back().name;
        if (tokens_[index].kind == TokenKind::KwNamespace && index + 2 < tokens_.size()) {
            std::size_t cursor = index + 1;
            std::string name;
            if (tokens_[cursor].kind == TokenKind::Identifier) {
                name = tokens_[cursor++].lexeme;
                while (cursor + 1 < tokens_.size() && tokens_[cursor].kind == TokenKind::Scope &&
                       tokens_[cursor + 1].kind == TokenKind::Identifier) {
                    name += "::" + tokens_[cursor + 1].lexeme;
                    cursor += 2;
                }
            }
            if (!name.empty() && cursor < tokens_.size() && tokens_[cursor].kind == TokenKind::LeftBrace) {
                ++braceDepth;
                namespaces.push_back({JoinName(active, name), braceDepth});
                index = cursor;
            }
            continue;
        }
        const bool declarationLevel = braceDepth == (namespaces.empty() ? 0 : namespaces.back().depth);
        if (!declarationLevel) continue;
        if ((tokens_[index].kind == TokenKind::KwClass ||
             tokens_[index].kind == TokenKind::KwInterface) && index + 1 < tokens_.size() &&
            tokens_[index + 1].kind == TokenKind::Identifier) {
            objectTypes_.insert(JoinName(active, tokens_[index + 1].lexeme));
        }
        if (tokens_[index].kind == TokenKind::KwEnum && index + 1 < tokens_.size() &&
            tokens_[index + 1].kind == TokenKind::Identifier) {
            enumTypes_.insert(JoinName(active, tokens_[index + 1].lexeme));
        }
        if (tokens_[index].kind == TokenKind::KwTypedef && index + 2 < tokens_.size()) {
            const auto primitive = TypedefPrimitive(tokens_[index + 1].kind);
            if (primitive && tokens_[index + 2].kind == TokenKind::Identifier)
                typedefTypes_[JoinName(active, tokens_[index + 2].lexeme)] = *primitive;
        }
    }
}

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
    if (Match(TokenKind::KwNamespace)) return ParseNamespace();
    if (Match(TokenKind::KwClass)) return ParseClass(false);
    if (Match(TokenKind::KwInterface)) return ParseClass(true);
    if (Match(TokenKind::KwEnum)) return ParseEnum();
    if (Match(TokenKind::KwTypedef)) return ParseTypedef();
    if (IsTypeStart() || Check(TokenKind::KwConst)) {
        const auto saved = current_;
        const bool returnConst = Match(TokenKind::KwConst);
        DataType type = ParseType(true);
        const bool returnsReference = Match(TokenKind::Amp);
        if (Check(TokenKind::Identifier)) {
            Token name = Advance();
            name.lexeme = QualifyDeclaration(name.lexeme);
            if (Check(TokenKind::LeftParen))
                return ParseFunction(type, std::move(name), returnsReference, returnConst);
        }
        current_ = saved;
    }
    AstNode* statement = ParseStatement();
    if (statement && statement->kind == NodeKind::VarDecl) {
        statement->isGlobal = true;
        statement->token.lexeme = QualifyDeclaration(statement->token.lexeme);
    }
    if (statement && statement->kind == NodeKind::DeclList) {
        for (AstNode* declaration = statement->firstChild; declaration;
             declaration = declaration->nextSibling) {
            declaration->isGlobal = true;
            declaration->token.lexeme = QualifyDeclaration(declaration->token.lexeme);
        }
    }
    return statement;
}

AstNode* Parser::ParseNamespace() {
    Token name = ParseQualifiedIdentifier("expected namespace name");
    const std::string previous = currentNamespace_;
    currentNamespace_ = JoinName(currentNamespace_, name.lexeme);
    name.lexeme = currentNamespace_;
    AstNode* declaration = arena_->Make(NodeKind::NamespaceDecl, name);
    Consume(TokenKind::LeftBrace, "expected '{' before namespace body");
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
        const auto before = current_;
        declaration->AppendChild(ParseTopLevel());
        if (current_ == before) { Error(Current(), "parser made no progress"); Advance(); }
    }
    Consume(TokenKind::RightBrace, "expected '}' after namespace body");
    currentNamespace_ = previous;
    return declaration;
}

AstNode* Parser::ParseEnum() {
    Token name = Consume(TokenKind::Identifier, "expected enum name");
    name.lexeme = QualifyDeclaration(name.lexeme);
    AstNode* declaration = arena_->Make(NodeKind::EnumDecl, name);
    Consume(TokenKind::LeftBrace, "expected '{' before enum body");
    if (!Check(TokenKind::RightBrace)) {
        do {
            Token valueName = Consume(TokenKind::Identifier, "expected enum value name");
            AstNode* value = arena_->Make(NodeKind::EnumValue, valueName);
            value->declaredType = DataType::Enum(name.lexeme);
            if (Match(TokenKind::Equal)) value->AppendChild(ParseExpression());
            declaration->AppendChild(value);
        } while (Match(TokenKind::Comma) && !Check(TokenKind::RightBrace));
    }
    Consume(TokenKind::RightBrace, "expected '}' after enum body");
    Match(TokenKind::Semicolon);
    return declaration;
}

AstNode* Parser::ParseTypedef() {
    const bool primitive = TypedefPrimitive(Current().kind).has_value();
    DataType source = ParseType(false);
    Token name = Consume(TokenKind::Identifier, "expected typedef name");
    name.lexeme = QualifyDeclaration(name.lexeme);
    AstNode* declaration = arena_->Make(NodeKind::TypedefDecl, name);
    declaration->declaredType = std::move(source);
    if (!primitive) Error(name, "typedef source must be a built-in primitive type");
    Consume(TokenKind::Semicolon, "expected ';' after typedef");
    return declaration;
}

AstNode* Parser::ParseClass(bool isInterface) {
    Token name = Consume(TokenKind::Identifier, "expected type name");
    const std::string simpleName = name.lexeme;
    name.lexeme = QualifyDeclaration(name.lexeme);
    AstNode* node = arena_->Make(isInterface ? NodeKind::InterfaceDecl : NodeKind::ClassDecl, name);
    if (Match(TokenKind::Colon)) {
        do {
            Token inheritedName = ParseQualifiedIdentifier("expected inherited type name");
            inheritedName.lexeme = ResolveTypeName(inheritedName.lexeme);
            node->AppendChild(arena_->Make(NodeKind::Identifier, inheritedName));
        } while (Match(TokenKind::Comma));
    }
    Consume(TokenKind::LeftBrace, "expected '{' before type body");
    while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
        MemberAccess access = MemberAccess::Public;
        Token accessToken;
        if (Match(TokenKind::KwPrivate)) {
            access = MemberAccess::Private;
            accessToken = Previous();
        } else if (Match(TokenKind::KwProtected)) {
            access = MemberAccess::Protected;
            accessToken = Previous();
        }
        if (isInterface && access != MemberAccess::Public)
            Error(accessToken, "interface members cannot be private or protected");
        if (Check(TokenKind::Tilde)) {
            const Token tilde = Advance();
            Token destructorName = Consume(TokenKind::Identifier, "expected destructor name after '~'");
            if (isInterface) Error(tilde, "interfaces cannot declare destructors");
            if (destructorName.lexeme != simpleName)
                Error(destructorName, "destructor name must match class '" + simpleName + "'");
            destructorName.lexeme = "~" + destructorName.lexeme;
            AstNode* destructor = ParseFunction(DataType::Void(), std::move(destructorName));
            destructor->isDestructor = true;
            destructor->memberAccess = access;
            node->AppendChild(destructor);
            continue;
        }
        if (!isInterface && Check(TokenKind::Identifier) && Current().lexeme == simpleName &&
            current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == TokenKind::LeftParen) {
            Token constructorName = Advance();
            AstNode* constructor = ParseFunction(DataType::Void(), std::move(constructorName));
            constructor->isConstructor = true;
            constructor->memberAccess = access;
            node->AppendChild(constructor);
            continue;
        }
        const bool returnConst = Match(TokenKind::KwConst);
        DataType type = ParseType(true);
        const bool returnsReference = Match(TokenKind::Amp);
        Token memberName = Consume(TokenKind::Identifier, "expected member name");
        if (Check(TokenKind::LeftParen)) {
            AstNode* method = ParseFunction(type, std::move(memberName), returnsReference, returnConst);
            method->memberAccess = access;
            node->AppendChild(method);
        } else if (Match(TokenKind::LeftBrace)) {
            bool sawGetter = false;
            bool sawSetter = false;
            while (!Check(TokenKind::RightBrace) && !Check(TokenKind::End)) {
                Token accessor = Consume(TokenKind::Identifier, "expected 'get' or 'set' accessor");
                const bool getter = accessor.lexeme == "get";
                const bool setter = accessor.lexeme == "set";
                if (!getter && !setter) Error(accessor, "expected 'get' or 'set' accessor");
                if ((getter && sawGetter) || (setter && sawSetter))
                    Error(accessor, "duplicate property accessor");
                sawGetter = sawGetter || getter;
                sawSetter = sawSetter || setter;
                AstNode* method = arena_->Make(NodeKind::FunctionDecl, accessor);
                method->token.lexeme = std::string(getter ? "get_" : "set_") + memberName.lexeme;
                method->declaredType = getter ? type : DataType::Void();
                method->memberAccess = access;
                method->propertyAccessor = true;
                if (setter) {
                    Token valueName{TokenKind::Identifier, "value", accessor.location};
                    AstNode* parameter = arena_->Make(NodeKind::Parameter, valueName);
                    parameter->declaredType = type;
                    method->AppendChild(parameter);
                }
                if (getter) Match(TokenKind::KwConst);
                if (!Match(TokenKind::Semicolon)) method->AppendChild(ParseBlock());
                node->AppendChild(method);
            }
            Consume(TokenKind::RightBrace, "expected '}' after property accessors");
            Match(TokenKind::Semicolon);
        } else {
            AstNode* field = arena_->Make(NodeKind::FieldDecl, memberName);
            field->declaredType = type;
            field->memberAccess = access;
            if (Match(TokenKind::Equal)) field->AppendChild(ParseExpression());
            Consume(TokenKind::Semicolon, "expected ';' after field");
            node->AppendChild(field);
        }
    }
    Consume(TokenKind::RightBrace, "expected '}' after type body");
    Match(TokenKind::Semicolon);
    return node;
}

AstNode* Parser::ParseFunction(DataType returnType, Token name, bool returnsReference,
                               bool returnReferenceConst) {
    AstNode* function = arena_->Make(NodeKind::FunctionDecl, name);
    function->declaredType = std::move(returnType);
    function->returnsReference = returnsReference;
    function->returnReferenceConst = returnReferenceConst;
    Consume(TokenKind::LeftParen, "expected '(' after function name");
    bool sawDefault = false;
    if (!Check(TokenKind::RightParen)) {
        do {
            DataType type = ParseType(false);
            ParameterMode mode = ParameterMode::Value;
            if (Match(TokenKind::Amp)) {
                if (Match(TokenKind::KwIn)) mode = ParameterMode::In;
                else if (Match(TokenKind::KwOut)) mode = ParameterMode::Out;
                else if (Match(TokenKind::KwInOut)) mode = ParameterMode::InOut;
                else mode = ParameterMode::InOut;
            }
            Token paramName = Consume(TokenKind::Identifier, "expected parameter name");
            AstNode* parameter = arena_->Make(NodeKind::Parameter, paramName);
            parameter->declaredType = std::move(type);
            parameter->parameterMode = mode;
            if (Match(TokenKind::Equal)) {
                sawDefault = true;
                parameter->AppendChild(ParseAssignment());
            } else if (sawDefault) {
                Error(paramName, "parameters after a default argument must also have defaults");
            }
            function->AppendChild(parameter);
        } while (Match(TokenKind::Comma));
    }
    Consume(TokenKind::RightParen, "expected ')' after parameters");
    while (Check(TokenKind::KwConst) ||
           (Check(TokenKind::Identifier) && Current().lexeme == "property")) {
        if (Match(TokenKind::KwConst)) continue;
        function->propertyAccessor = true;
        Advance();
    }
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
    if (Match(TokenKind::KwBreak)) {
        AstNode* statement = arena_->Make(NodeKind::BreakStmt, Previous());
        Consume(TokenKind::Semicolon, "expected ';' after break");
        return statement;
    }
    if (Match(TokenKind::KwContinue)) {
        AstNode* statement = arena_->Make(NodeKind::ContinueStmt, Previous());
        Consume(TokenKind::Semicolon, "expected ';' after continue");
        return statement;
    }
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
    AstNode* left = ParseConditional();
    if (!MatchAny({TokenKind::Equal, TokenKind::PlusEqual, TokenKind::MinusEqual,
                   TokenKind::StarEqual, TokenKind::StarStarEqual,
                   TokenKind::SlashEqual, TokenKind::PercentEqual,
                   TokenKind::AmpEqual, TokenKind::PipeEqual, TokenKind::CaretEqual,
                   TokenKind::ShiftLeftEqual, TokenKind::ShiftRightEqual,
                   TokenKind::ShiftRightArithmeticEqual})) return left;
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
BINARY_LEVEL(ParseAnd, ParseBitOr, TokenKind::AndAnd)
BINARY_LEVEL(ParseBitOr, ParseBitXor, TokenKind::Pipe)
BINARY_LEVEL(ParseBitXor, ParseBitAnd, TokenKind::Caret)
BINARY_LEVEL(ParseBitAnd, ParseEquality, TokenKind::Amp)
BINARY_LEVEL(ParseEquality, ParseComparison, TokenKind::EqualEqual, TokenKind::BangEqual, TokenKind::KwIs)
BINARY_LEVEL(ParseComparison, ParseShift, TokenKind::Less, TokenKind::LessEqual, TokenKind::Greater, TokenKind::GreaterEqual)
BINARY_LEVEL(ParseShift, ParseTerm, TokenKind::ShiftLeft, TokenKind::ShiftRight, TokenKind::ShiftRightArithmetic)
BINARY_LEVEL(ParseTerm, ParseFactor, TokenKind::Plus, TokenKind::Minus)
BINARY_LEVEL(ParseFactor, ParsePower, TokenKind::Star, TokenKind::Slash, TokenKind::Percent)
BINARY_LEVEL(ParsePower, ParseUnary, TokenKind::StarStar)
#undef BINARY_LEVEL

AstNode* Parser::ParseUnary() {
    if (MatchAny({TokenKind::PlusPlus, TokenKind::MinusMinus})) {
        AstNode* node = arena_->Make(NodeKind::Increment, Previous());
        node->AppendChild(ParseUnary());
        return node;
    }
    if (MatchAny({TokenKind::Bang, TokenKind::Tilde, TokenKind::Minus, TokenKind::Plus, TokenKind::At})) {
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
            bool sawNamedArgument = false;
            if (!Check(TokenKind::RightParen)) {
                do {
                    if (Check(TokenKind::Identifier) && current_ + 1 < tokens_.size() &&
                        tokens_[current_ + 1].kind == TokenKind::Colon) {
                        sawNamedArgument = true;
                        AstNode* named = arena_->Make(NodeKind::NamedArgument, Advance());
                        Advance();
                        named->AppendChild(ParseAssignment());
                        call->AppendChild(named);
                    } else {
                        if (sawNamedArgument)
                            Error(Current(), "positional arguments cannot follow named arguments");
                        call->AppendChild(ParseExpression());
                    }
                } while (Match(TokenKind::Comma));
            }
            Consume(TokenKind::RightParen, "expected ')' after arguments");
            expression = call;
        } else if (Match(TokenKind::Dot)) {
            AstNode* member = arena_->Make(NodeKind::Member,
                            Consume(TokenKind::Identifier, "expected member name"));
            member->AppendChild(expression);
            expression = member;
        } else if (MatchAny({TokenKind::PlusPlus, TokenKind::MinusMinus})) {
            AstNode* increment = arena_->Make(NodeKind::Increment, Previous());
            increment->isPostfix = true;
            increment->AppendChild(expression);
            expression = increment;
        } else break;
    }
    return expression;
}

AstNode* Parser::ParsePrimary() {
    if (IsTypeStart(false) && !Check(TokenKind::Identifier)) {
        const Token castToken = Current();
        AstNode* cast = arena_->Make(NodeKind::ValueCast, castToken);
        cast->declaredType = ParseType(false);
        Consume(TokenKind::LeftParen, "expected '(' after value cast type");
        cast->AppendChild(ParseExpression());
        Consume(TokenKind::RightParen, "expected ')' after value cast expression");
        return cast;
    }
    if (MatchAny({TokenKind::Integer, TokenKind::Bits, TokenKind::Float, TokenKind::Double,
                  TokenKind::String,
                  TokenKind::KwTrue, TokenKind::KwFalse, TokenKind::KwNull})) {
        return arena_->Make(NodeKind::Literal, Previous());
    }
    if (Match(TokenKind::KwCast)) {
        const Token castToken = Previous();
        AstNode* cast = arena_->Make(NodeKind::Cast, castToken);
        Consume(TokenKind::Less, "expected '<' after 'cast'");
        cast->declaredType = ParseType(false);
        cast->declaredType.isHandle = true;
        Consume(TokenKind::Greater, "expected '>' after cast target type");
        Consume(TokenKind::LeftParen, "expected '(' after cast target type");
        cast->AppendChild(ParseExpression());
        Consume(TokenKind::RightParen, "expected ')' after cast expression");
        return cast;
    }
    if (Check(TokenKind::Identifier))
        return arena_->Make(NodeKind::Identifier, ParseQualifiedIdentifier("expected identifier"));
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
    else if (Match(TokenKind::KwInt8)) type = DataType::Int8();
    else if (Match(TokenKind::KwInt16)) type = DataType::Int16();
    else if (Match(TokenKind::KwInt)) type = DataType::Int();
    else if (Match(TokenKind::KwInt64)) type = DataType::Int64();
    else if (Match(TokenKind::KwUInt8)) type = DataType::UInt8();
    else if (Match(TokenKind::KwUInt16)) type = DataType::UInt16();
    else if (Match(TokenKind::KwUInt)) type = DataType::UInt();
    else if (Match(TokenKind::KwUInt64)) type = DataType::UInt64();
    else if (Match(TokenKind::KwFloat)) type = DataType::Float();
    else if (Match(TokenKind::KwDouble)) type = DataType::Double();
    else if (Match(TokenKind::KwString)) type = DataType::String();
    else if (Check(TokenKind::Identifier)) {
        Token identifier = ParseQualifiedIdentifier("expected type");
        const std::string name = ResolveTypeName(identifier.lexeme);
        const auto alias = typedefTypes_.find(name);
        if (alias != typedefTypes_.end()) type = alias->second;
        else type = enumTypes_.find(name) != enumTypes_.end() ? DataType::Enum(name)
                                                              : DataType::Object(name);
    }
    else { Error(Current(), "expected type"); return DataType::Invalid(); }
    if (Match(TokenKind::At)) type.isHandle = true;
    return type;
}

bool Parser::IsTypeStart(bool allowIdentifier) const {
    switch (Current().kind) {
    case TokenKind::KwVoid: case TokenKind::KwBool:
    case TokenKind::KwInt8: case TokenKind::KwInt16: case TokenKind::KwInt: case TokenKind::KwInt64:
    case TokenKind::KwUInt8: case TokenKind::KwUInt16: case TokenKind::KwUInt: case TokenKind::KwUInt64:
    case TokenKind::KwFloat: case TokenKind::KwDouble: case TokenKind::KwString: return true;
    case TokenKind::Identifier: return allowIdentifier;
    default: return false;
    }
}

Token Parser::ParseQualifiedIdentifier(const char* message) {
    Token result = Consume(TokenKind::Identifier, message);
    while (Match(TokenKind::Scope)) {
        Token part = Consume(TokenKind::Identifier, "expected identifier after '::'");
        result.lexeme += "::" + part.lexeme;
    }
    return result;
}

std::string Parser::QualifyDeclaration(std::string_view name) const {
    return name.find("::") == std::string_view::npos ? JoinName(currentNamespace_, name)
                                                     : std::string(name);
}

std::string Parser::ResolveTypeName(std::string_view name) const {
    if (name.find("::") != std::string_view::npos) return std::string(name);
    std::string scope = currentNamespace_;
    for (;;) {
        const std::string candidate = JoinName(scope, name);
        if (enumTypes_.find(candidate) != enumTypes_.end() ||
            objectTypes_.find(candidate) != objectTypes_.end() ||
            typedefTypes_.find(candidate) != typedefTypes_.end()) return candidate;
        if (scope.empty()) break;
        const auto separator = scope.rfind("::");
        scope = separator == std::string::npos ? std::string{} : scope.substr(0, separator);
    }
    return std::string(name);
}

AstNode* Parser::ParseConditional() {
    AstNode* condition = ParseOr();
    if (!Match(TokenKind::Question)) return condition;
    AstNode* node = arena_->Make(NodeKind::Conditional, Previous());
    node->AppendChild(condition);
    node->AppendChild(ParseExpression());
    Consume(TokenKind::Colon, "expected ':' in conditional expression");
    node->AppendChild(ParseAssignment());
    return node;
}

bool Parser::IsVariableDeclarationStart() const {
    if (Check(TokenKind::KwConst) || Check(TokenKind::KwAuto)) return true;
    if (!IsTypeStart()) return false;
    if (Current().kind != TokenKind::Identifier) return true;
    std::size_t cursor = current_ + 1;
    while (cursor + 1 < tokens_.size() && tokens_[cursor].kind == TokenKind::Scope &&
           tokens_[cursor + 1].kind == TokenKind::Identifier) cursor += 2;
    if (cursor < tokens_.size() && tokens_[cursor].kind == TokenKind::At) ++cursor;
    return cursor < tokens_.size() && tokens_[cursor].kind == TokenKind::Identifier;
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
        case TokenKind::KwDefault: case TokenKind::KwReturn: case TokenKind::KwBreak:
        case TokenKind::KwContinue:
        case TokenKind::KwClass: case TokenKind::KwInterface: case TokenKind::KwEnum:
        case TokenKind::KwTypedef: case TokenKind::KwNamespace: return;
        default: Advance();
        }
    }
}

} // namespace mini_as

