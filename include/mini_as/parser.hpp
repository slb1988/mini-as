#pragma once

#include "mini_as/tokenizer.hpp"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mini_as {

enum class ParameterMode { Value, In, Out, InOut };
enum class MemberAccess { Public, Protected, Private };

enum class NodeKind {
    Program, NamespaceDecl, FunctionDecl, Parameter, ClassDecl, InterfaceDecl,
    EnumDecl, EnumValue, TypedefDecl, FuncdefDecl, FieldDecl,
    Block, DeclList, VarDecl, IfStmt, WhileStmt, DoWhileStmt, ForStmt,
    SwitchStmt, CaseClause, DefaultClause, ReturnStmt, BreakStmt, ContinueStmt, TryStmt,
    ExprStmt, EmptyStmt,
    Assign, Conditional, Binary, Unary, Increment, Cast, ValueCast, Call, NamedArgument,
    AnonymousFunction, Member, Literal, Identifier
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
    bool isDestructor = false;
    bool isDeleted = false;
    bool hasExplicitSuper = false;
    bool nonVirtualCall = false;
    bool returnsReference = false;
    bool returnReferenceConst = false;
    bool propertyAccessor = false;
    std::string operatorMethod;
    bool operatorReversed = false;
    std::string propertyGetter;
    std::string propertySetter;
    std::string delegateObjectType;
    std::vector<std::string> captureNames;
    MemberAccess memberAccess = MemberAccess::Public;
    ParameterMode parameterMode = ParameterMode::Value;
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
    AstNode* ParseNamespace();
    AstNode* ParseClass(bool isInterface);
    AstNode* ParseEnum();
    AstNode* ParseTypedef();
    AstNode* ParseFuncdef(std::string_view parentType = {});
    AstNode* ParseFunction(DataType returnType, Token name, bool returnsReference = false,
                           bool returnReferenceConst = false);
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
    AstNode* ParseBitOr();
    AstNode* ParseBitXor();
    AstNode* ParseBitAnd();
    AstNode* ParseEquality();
    AstNode* ParseComparison();
    AstNode* ParseShift();
    AstNode* ParseTerm();
    AstNode* ParseFactor();
    AstNode* ParsePower();
    AstNode* ParseUnary();
    AstNode* ParseCall();
    AstNode* ParsePrimary();
    AstNode* ParseAnonymousFunction();
    DataType ParseType(bool allowVoid = false);
    Token ParseQualifiedIdentifier(const char* message);
    std::string QualifyDeclaration(std::string_view name) const;
    std::string ResolveTypeName(std::string_view name) const;

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
    std::unordered_set<std::string> enumTypes_;
    std::unordered_set<std::string> objectTypes_;
    std::unordered_set<std::string> funcdefTypes_;
    std::unordered_map<std::string, std::vector<std::string>> objectBases_;
    std::unordered_map<std::string, DataType> typedefTypes_;
    std::string currentNamespace_;
    std::string currentTypeName_;
};

} // namespace mini_as

