#pragma once

#include "mini_as/parser.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace mini_as {

struct FunctionSignature {
    std::string name;
    DataType returnType;
    std::vector<DataType> parameters;
    bool host = false;

    std::string Declaration() const;
};

class TypeChecker {
public:
    explicit TypeChecker(DiagnosticSink& diagnostics);
    void RegisterFunction(FunctionSignature signature);
    bool Check(AstNode* root);
    const std::vector<FunctionSignature>& Functions() const;

private:
    void Predeclare(AstNode* root);
    void CheckNode(AstNode* node);
    void CheckFunction(AstNode* node);
    void CheckBlock(AstNode* node, bool createScope = true);
    DataType CheckExpression(AstNode* node);
    DataType CheckBinary(AstNode* node);
    DataType CheckUnary(AstNode* node);
    DataType CheckCall(AstNode* node);
    std::optional<DataType> Lookup(std::string_view name) const;
    void Declare(const Token& name, const DataType& type);
    bool CanConvert(const DataType& from, const DataType& to) const;
    void Error(const AstNode* node, std::string message);

    DiagnosticSink& diagnostics_;
    std::vector<FunctionSignature> functions_;
    std::vector<std::unordered_map<std::string, DataType>> scopes_;
    DataType currentReturn_ = DataType::Void();
};

} // namespace mini_as

