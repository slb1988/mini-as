#pragma once

#include "mini_as/tokenizer.hpp"

#include <memory>
#include <vector>

namespace mini_as {

enum class NodeKind {
    Program, FunctionDecl, Parameter, ClassDecl, InterfaceDecl, FieldDecl,
    Block, DeclList, VarDecl, IfStmt, WhileStmt, DoWhileStmt, ForStmt,
    SwitchStmt, CaseClause, DefaultClause, ReturnStmt, BreakStmt, ContinueStmt, ExprStmt, EmptyStmt,
    Assign, Conditional, Binary, Unary, Increment, Call, Member, Literal, Identifier
};

struct AstNode {
    NodeKind kind = NodeKind::Program;
    Token token;
    DataType declaredType = DataType::Invalid();
    DataType inferredType = DataType::Invalid();
    bool isConst = false;
    bool isAuto = false;
    bool isGlobal = false;
    bool isPostfix = false;
    bool implicitThis = false;
    bool isConstructor = false;
    AstNode* firstChild = nullptr;
    AstNode* nextSibling = nullptr;

    void AppendChild(AstNode* child);
    std::vector<AstNode*> Children() const;
};

class AstArena {
public:
    AstNode* Make(NodeKind kind, const Token& token = {});

private:
    std::vector<std::unique_ptr<AstNode>> nodes_;
};

struct SyntaxTree {
    AstArena arena;
    AstNode* root = nullptr;
};

class Parser {
public:
    Parser(std::vector<Token> tokens, DiagnosticSink& diagnostics);
    SyntaxTree Parse();

private:
    AstNode* ParseTopLevel();
    AstNode* ParseClass(bool isInterface);
    AstNode* ParseFunction(DataType returnType, Token name);
    AstNode* ParseBlock();
    AstNode* ParseStatement();
    AstNode* ParseVariableDeclaration();
    AstNode* ParseIf();
    AstNode* ParseWhile();
    AstNode* ParseDoWhile();
    AstNode* ParseFor();
    AstNode* ParseSwitch();
    AstNode* ParseReturn();
    AstNode* ParseExpression();
    AstNode* ParseAssignment();
    AstNode* ParseConditional();
    AstNode* ParseOr();
    AstNode* ParseAnd();
    AstNode* ParseEquality();
    AstNode* ParseComparison();
    AstNode* ParseTerm();
    AstNode* ParseFactor();
    AstNode* ParseUnary();
    AstNode* ParseCall();
    AstNode* ParsePrimary();
    DataType ParseType(bool allowVoid = false);

    bool IsTypeStart(bool allowIdentifier = true) const;
    bool IsVariableDeclarationStart() const;
    bool Match(TokenKind kind);
    bool MatchAny(std::initializer_list<TokenKind> kinds);
    bool Check(TokenKind kind) const;
    const Token& Advance();
    const Token& Current() const;
    const Token& Previous() const;
    Token Consume(TokenKind kind, const char* message);
    void Error(const Token& token, std::string message);
    void Synchronize();

    std::vector<Token> tokens_;
    DiagnosticSink& diagnostics_;
    std::size_t current_ = 0;
    AstArena* arena_ = nullptr;
};

} // namespace mini_as

