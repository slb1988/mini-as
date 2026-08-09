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
    std::size_t defaultArgumentCount = 0;
    std::vector<std::string> parameterNames;
    std::vector<ParameterMode> parameterModes;
    bool returnsReference = false;
    bool returnReferenceConst = false;
    bool destructor = false;
    MemberAccess access = MemberAccess::Public;
    bool propertyAccessor = false;
    bool factory = false;
    bool readOnlyMethod = false;

    std::string Declaration() const;
};

struct FieldSignature {
    std::string name;
    DataType type;
    std::string objectType;
    MemberAccess access = MemberAccess::Public;
    bool isConst = false;
    bool host = false;
};

struct ClassSignature {
    std::string name;
    bool interfaceType = false;
    std::vector<std::string> inheritedTypes;
    std::string baseClass;
    std::vector<std::string> interfaces;
    std::vector<FieldSignature> fields;
    std::size_t inheritedFieldCount = 0;
    std::vector<FunctionSignature> methods;
    bool defaultConstructorDeleted = false;
    bool defaultCopyConstructorDeleted = false;
    bool defaultCopyAssignmentDeleted = false;
    bool generatedCopyConstructor = false;
    TypeId id;
    bool host = false;
};

struct GlobalSignature {
    std::string name;
    DataType type;
    bool isConst = false;
    GlobalId id;
    bool host = false;
};

struct EnumValueSignature {
    std::string name;
    std::int32_t value = 0;
};

struct EnumSignature {
    std::string name;
    std::vector<EnumValueSignature> values;
    TypeId id;
};

struct TypedefSignature {
    std::string name;
    DataType underlyingType;
    TypeId id;
};

struct FuncdefSignature {
    std::string name;
    FunctionSignature signature;
    TypeId id;
    std::string parentType;
};

class TypeChecker {
public:
    explicit TypeChecker(DiagnosticSink& diagnostics);
    void RegisterFunction(FunctionSignature signature);
    void RegisterGlobalProperty(GlobalSignature signature);
    void RegisterObjectType(ClassSignature signature);
    bool Check(AstNode* root);
    const std::vector<FunctionSignature>& Functions() const;
    const std::vector<ClassSignature>& Classes() const;
    const std::vector<GlobalSignature>& Globals() const;
    const std::vector<EnumSignature>& Enums() const;
    const std::vector<TypedefSignature>& Typedefs() const;
    const std::vector<FuncdefSignature>& Funcdefs() const;

private:
    struct VariableSymbol {
        DataType type;
        bool isConst = false;
        bool returnableReference = false;
    };

    void PredeclareTypedefs(AstNode* root);
    void PredeclareEnums(AstNode* root);
    void PredeclareFuncdefs(AstNode* root);
    void Predeclare(AstNode* root);
    void PredeclareGlobals(AstNode* root);
    void CheckNode(AstNode* node);
    void CheckFunction(AstNode* node);
    void CheckBlock(AstNode* node, bool createScope = true);
    DataType CheckExpression(AstNode* node, std::optional<DataType> expected = std::nullopt);
    DataType CheckMember(AstNode* node, bool writing = false, bool compound = false);
    DataType CheckImplicitProperty(AstNode* node, bool writing = false, bool compound = false);
    DataType CheckBinary(AstNode* node);
    DataType CheckUnary(AstNode* node, std::optional<DataType> expected = std::nullopt);
    DataType CheckCall(AstNode* node);
    DataType CheckAnonymousFunction(AstNode* node, std::optional<DataType> expected);
    std::optional<VariableSymbol> Lookup(std::string_view name) const;
    std::optional<Value> FindEnumConstant(std::string_view name) const;
    bool IsReadOnlyLValue(const AstNode* node) const;
    const FunctionSignature* FindMethod(const DataType& object, std::string_view name,
                                        const std::vector<DataType>& arguments,
                                        const std::vector<std::string>& argumentNames) const;
    const FunctionSignature* FindMethodInClass(const ClassSignature* type, std::string_view name,
                                               const std::vector<DataType>& arguments,
                                               const std::vector<std::string>& argumentNames) const;
    const FunctionSignature* FindExactMethod(const ClassSignature* type, std::string_view name,
                                             const std::vector<DataType>& parameters,
                                             const std::vector<ParameterMode>& modes) const;
    const FunctionSignature* FindOperatorMethod(const DataType& object, std::string_view name,
                                                const std::vector<DataType>& arguments,
                                                std::optional<DataType> requiredReturn = std::nullopt) const;
    std::optional<int> MatchArguments(const FunctionSignature& signature,
                                      const std::vector<DataType>& arguments,
                                      const std::vector<std::string>& argumentNames) const;
    bool ValidateReferenceArguments(const FunctionSignature& signature,
                                    const std::vector<AstNode*>& arguments,
                                    const std::vector<std::string>& argumentNames);
    const ClassSignature* FindClass(std::string_view name) const;
    const FuncdefSignature* FindFuncdef(std::string_view name) const;
    const FunctionSignature* ResolveFunctionAddress(AstNode* node,
                                                     std::optional<DataType> expected,
                                                     bool reportErrors = true);
    bool IsDerivedFrom(std::string_view derived, std::string_view base) const;
    bool CanAccess(MemberAccess access, std::string_view declaringType) const;
    void CheckAccess(const AstNode* node, MemberAccess access, std::string_view declaringType,
                     std::string_view memberKind, std::string_view memberName);
    void Declare(const Token& name, const DataType& type, bool isConst = false,
                 bool returnableReference = false);
    bool CanReturnReference(const AstNode* node) const;
    bool CanConvert(const DataType& from, const DataType& to) const;
    std::optional<int> ConversionCost(const DataType& from, const DataType& to) const;
    void Error(const AstNode* node, std::string message);

    DiagnosticSink& diagnostics_;
    std::vector<FunctionSignature> functions_;
    std::vector<GlobalSignature> registeredGlobals_;
    std::vector<ClassSignature> registeredClasses_;
    std::vector<ClassSignature> classes_;
    std::vector<GlobalSignature> globals_;
    std::vector<EnumSignature> enums_;
    std::vector<TypedefSignature> typedefs_;
    std::vector<FuncdefSignature> funcdefs_;
    std::unordered_map<std::string, Value> enumConstants_;
    std::vector<std::unordered_map<std::string, VariableSymbol>> scopes_;
    DataType currentReturn_ = DataType::Void();
    bool currentReturnsReference_ = false;
    int breakableDepth_ = 0;
    int loopDepth_ = 0;
    const ClassSignature* currentClass_ = nullptr;
    bool currentConstructor_ = false;
    int superCallCount_ = 0;
    std::string currentNamespace_;
    mutable std::vector<std::pair<AstNode*, std::size_t>> activeLambdas_;
};

} // namespace mini_as
