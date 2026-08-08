#pragma once

#include "mini_as/parser.hpp"
#include "mini_as/symbols.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace mini_as {

struct FunctionSignature {
    std::string name;
    DataType returnType;
    std::vector<DataType> parameters;
    bool host = false;
    FunctionId id;
    std::string objectType;
    bool method = false;
    bool constructor = false;

    std::string Declaration() const;
};

struct ClassSignature {
    std::string name;
    bool interfaceType = false;
    std::vector<std::string> interfaces;
    std::vector<std::pair<std::string, DataType>> fields;
    std::vector<FunctionSignature> methods;
    TypeId id;
};

struct GlobalSignature {
    std::string name;
    DataType type;
    bool isConst = false;
    GlobalId id;
};

class TypeChecker {
public:
    explicit TypeChecker(DiagnosticSink& diagnostics);
    void RegisterFunction(FunctionSignature signature);
    bool Check(AstNode* root);
    const std::vector<FunctionSignature>& Functions() const;
    const std::vector<ClassSignature>& Classes() const;
    const std::vector<GlobalSignature>& Globals() const;

private:
    struct VariableSymbol {
        DataType type;
        bool isConst = false;
    };

    void Predeclare(AstNode* root);
    void PredeclareGlobals(AstNode* root);
    void CheckNode(AstNode* node);
    void CheckFunction(AstNode* node);
    void CheckBlock(AstNode* node, bool createScope = true);
    DataType CheckExpression(AstNode* node);
    DataType CheckBinary(AstNode* node);
    DataType CheckUnary(AstNode* node);
    DataType CheckCall(AstNode* node);
    std::optional<VariableSymbol> Lookup(std::string_view name) const;
    bool IsReadOnlyLValue(const AstNode* node) const;
    const FunctionSignature* FindMethod(const DataType& object, std::string_view name,
                                        const std::vector<DataType>& arguments) const;
    const ClassSignature* FindClass(std::string_view name) const;
    void Declare(const Token& name, const DataType& type, bool isConst = false);
    bool CanConvert(const DataType& from, const DataType& to) const;
    void Error(const AstNode* node, std::string message);

    DiagnosticSink& diagnostics_;
    std::vector<FunctionSignature> functions_;
    std::vector<ClassSignature> classes_;
    std::vector<GlobalSignature> globals_;
    std::vector<std::unordered_map<std::string, VariableSymbol>> scopes_;
    DataType currentReturn_ = DataType::Void();
    int breakableDepth_ = 0;
    int loopDepth_ = 0;
    const ClassSignature* currentClass_ = nullptr;
};

} // namespace mini_as
