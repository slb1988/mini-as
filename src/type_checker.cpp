#include "mini_as/type_checker.hpp"
#include "mini_as/constant_evaluator.hpp"

#include <limits>
#include <functional>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace mini_as {
namespace {

void CollectDeclarations(AstNode* owner, std::vector<AstNode*>& result) {
    if (!owner) return;
    for (AstNode* node = owner->firstChild; node; node = node->nextSibling) {
        if (node->kind == NodeKind::NamespaceDecl) CollectDeclarations(node, result);
        else result.push_back(node);
    }
}

std::vector<AstNode*> TopLevelDeclarations(AstNode* root) {
    std::vector<AstNode*> result;
    CollectDeclarations(root, result);
    return result;
}

std::string NamespaceOf(std::string_view qualifiedName) {
    const auto separator = qualifiedName.rfind("::");
    return separator == std::string_view::npos ? std::string{}
                                               : std::string(qualifiedName.substr(0, separator));
}

std::vector<std::string> NameCandidates(std::string_view nameSpace, std::string_view name) {
    if (name.find("::") != std::string_view::npos) return {std::string(name)};
    std::vector<std::string> result;
    std::string scope(nameSpace);
    while (!scope.empty()) {
        result.push_back(scope + "::" + std::string(name));
        const auto separator = scope.rfind("::");
        scope = separator == std::string::npos ? std::string{} : scope.substr(0, separator);
    }
    result.push_back(std::string(name));
    return result;
}

std::optional<int> NameMatchCost(std::string_view candidate, std::string_view requested,
                                 std::string_view nameSpace) {
    const auto names = NameCandidates(nameSpace, requested);
    for (std::size_t index = 0; index < names.size(); ++index)
        if (candidate == names[index]) return static_cast<int>(index) * 10;
    return std::nullopt;
}

} // namespace

std::string FunctionSignature::Declaration() const {
    std::ostringstream out;
    if (destructor) return name + "()";
    if (returnReferenceConst) out << "const ";
    out << returnType.Name();
    if (returnsReference) out << " &";
    else out << ' ';
    out << name << '(';
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i) out << ", ";
        out << parameters[i].Name();
        const ParameterMode mode = i < parameterModes.size()
            ? parameterModes[i] : ParameterMode::Value;
        if (mode == ParameterMode::In) out << " &in";
        else if (mode == ParameterMode::Out) out << " &out";
        else if (mode == ParameterMode::InOut) out << " &inout";
    }
    return out.str() + ')';
}

TypeChecker::TypeChecker(DiagnosticSink& diagnostics) : diagnostics_(diagnostics) {}

void TypeChecker::RegisterFunction(FunctionSignature signature) {
    functions_.push_back(std::move(signature));
}

bool TypeChecker::Check(AstNode* root) {
    scopes_.clear();
    scopes_.emplace_back();
    PredeclareTypedefs(root);
    PredeclareEnums(root);
    Predeclare(root);
    PredeclareGlobals(root);
    for (AstNode* child : TopLevelDeclarations(root)) CheckNode(child);
    return !diagnostics_.HasErrors();
}

const std::vector<FunctionSignature>& TypeChecker::Functions() const { return functions_; }
const std::vector<ClassSignature>& TypeChecker::Classes() const { return classes_; }
const std::vector<GlobalSignature>& TypeChecker::Globals() const { return globals_; }
const std::vector<EnumSignature>& TypeChecker::Enums() const { return enums_; }
const std::vector<TypedefSignature>& TypeChecker::Typedefs() const { return typedefs_; }

void TypeChecker::PredeclareTypedefs(AstNode* root) {
    typedefs_.clear();
    if (!root) return;
    for (AstNode* node : TopLevelDeclarations(root)) {
        if (node->kind != NodeKind::TypedefDecl) continue;
        bool duplicate = false;
        for (const auto& existing : typedefs_) {
            if (existing.name == node->token.lexeme) duplicate = true;
        }
        if (duplicate) Error(node, "duplicate typedef '" + node->token.lexeme + "'");
        else typedefs_.push_back({node->token.lexeme, node->declaredType, {}});
    }
}

void TypeChecker::PredeclareEnums(AstNode* root) {
    enums_.clear();
    enumConstants_.clear();
    if (!root) return;
    for (AstNode* node : TopLevelDeclarations(root)) {
        if (node->kind != NodeKind::EnumDecl) continue;
        bool duplicateType = false;
        for (const auto& existing : enums_) {
            if (existing.name == node->token.lexeme) duplicateType = true;
        }
        if (duplicateType) {
            Error(node, "duplicate enum '" + node->token.lexeme + "'");
            continue;
        }
        EnumSignature signature;
        signature.name = node->token.lexeme;
        const DataType enumType = DataType::Enum(signature.name);
        const std::string enumNamespace = NamespaceOf(signature.name);
        std::int64_t nextValue = 0;
        std::unordered_set<std::string> localNames;
        for (AstNode* valueNode = node->firstChild; valueNode; valueNode = valueNode->nextSibling) {
            valueNode->declaredType = enumType;
            const std::string qualifiedValue = enumNamespace.empty()
                ? valueNode->token.lexeme : enumNamespace + "::" + valueNode->token.lexeme;
            if (!localNames.insert(valueNode->token.lexeme).second ||
                enumConstants_.find(qualifiedValue) != enumConstants_.end()) {
                Error(valueNode, "duplicate enum value '" + valueNode->token.lexeme + "'");
                continue;
            }
            if (valueNode->firstChild) {
                ConstantExpressionEvaluator evaluator([this, &enumNamespace](std::string_view name) {
                    for (const auto& candidate : NameCandidates(enumNamespace, name)) {
                        const auto found = enumConstants_.find(candidate);
                        if (found != enumConstants_.end()) return std::optional<Value>{found->second};
                    }
                    return std::optional<Value>{};
                });
                const auto explicitValue = evaluator.Evaluate(valueNode->firstChild);
                if (!explicitValue || !explicitValue->Type().IsInteger()) {
                    Error(valueNode->firstChild, "enum value must be an integer constant expression");
                    continue;
                }
                if (explicitValue->Type().IsSignedInteger()) {
                    nextValue = explicitValue->SignedInteger();
                } else if (explicitValue->UnsignedInteger() <=
                           static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
                    nextValue = static_cast<std::int64_t>(explicitValue->UnsignedInteger());
                } else {
                    Error(valueNode->firstChild, "enum value is outside the int32 range");
                    continue;
                }
            }
            if (nextValue < std::numeric_limits<std::int32_t>::min() ||
                nextValue > std::numeric_limits<std::int32_t>::max()) {
                Error(valueNode, "enum value is outside the int32 range");
                continue;
            }
            const auto stored = static_cast<std::int32_t>(nextValue);
            signature.values.push_back({valueNode->token.lexeme, stored});
            enumConstants_.emplace(qualifiedValue,
                                   Value::Integer(enumType, static_cast<std::uint32_t>(stored)));
            ++nextValue;
        }
        enums_.push_back(std::move(signature));
    }
}

void TypeChecker::Predeclare(AstNode* root) {
    if (!root) return;
    classes_.clear();
    for (AstNode* node : TopLevelDeclarations(root)) {
        if (node->kind != NodeKind::ClassDecl && node->kind != NodeKind::InterfaceDecl) continue;
        ClassSignature type;
        type.name = node->token.lexeme;
        type.interfaceType = node->kind == NodeKind::InterfaceDecl;
        AstNode* child = node->firstChild;
        while (child && child->kind == NodeKind::Identifier) {
            type.inheritedTypes.push_back(child->token.lexeme);
            child = child->nextSibling;
        }
        for (; child; child = child->nextSibling) {
            if (child->kind == NodeKind::FieldDecl) type.fields.push_back({child->token.lexeme, child->declaredType});
            else if (child->kind == NodeKind::FunctionDecl) {
                FunctionSignature method{child->token.lexeme, child->declaredType, {}, false, {},
                                         type.name, true, child->isConstructor, 0, {}, {},
                                         child->returnsReference, child->returnReferenceConst,
                                         child->isDestructor};
                for (AstNode* parameter = child->firstChild;
                     parameter && parameter->kind == NodeKind::Parameter; parameter = parameter->nextSibling) {
                    method.parameters.push_back(parameter->declaredType);
                    method.parameterNames.push_back(parameter->token.lexeme);
                    method.parameterModes.push_back(parameter->parameterMode);
                    if (parameter->firstChild) ++method.defaultArgumentCount;
                }
                bool duplicate = false;
                for (const auto& existing : type.methods) {
                    if (existing.name == method.name && existing.parameters == method.parameters &&
                        existing.constructor == method.constructor &&
                        existing.destructor == method.destructor) duplicate = true;
                }
                if (duplicate) Error(child, "duplicate method or constructor '" + method.Declaration() + "'");
                if (method.destructor && !method.parameters.empty())
                    Error(child, "destructor cannot declare parameters");
                AstNode* body = child->firstChild;
                while (body && body->kind == NodeKind::Parameter) body = body->nextSibling;
                if (method.destructor && (!body || body->kind != NodeKind::Block))
                    Error(child, "destructor must have a body");
                type.methods.push_back(std::move(method));
            }
        }
        classes_.push_back(std::move(type));
    }

    std::unordered_map<std::string, int> inheritanceState;
    std::function<void(ClassSignature&)> resolveInheritance = [&](ClassSignature& type) {
        int& state = inheritanceState[type.name];
        if (state == 2) return;
        if (state == 1) {
            Error(root, "cyclic class inheritance involving '" + type.name + "'");
            type.baseClass.clear();
            state = 2;
            return;
        }
        state = 1;
        for (const auto& inheritedName : type.inheritedTypes) {
            ClassSignature* inherited = nullptr;
            for (auto& candidate : classes_)
                if (candidate.name == inheritedName) inherited = &candidate;
            if (!inherited) {
                Error(root, "unknown inherited type '" + inheritedName + "'");
                continue;
            }
            if (inherited == &type) {
                Error(root, "class '" + type.name + "' cannot inherit from itself");
                continue;
            }
            if (inherited->interfaceType) {
                if (std::find(type.interfaces.begin(), type.interfaces.end(), inheritedName) ==
                    type.interfaces.end()) type.interfaces.push_back(inheritedName);
                continue;
            }
            if (type.interfaceType) {
                Error(root, "interface '" + type.name + "' cannot inherit from class '" + inheritedName + "'");
                continue;
            }
            if (!type.baseClass.empty()) {
                Error(root, "class '" + type.name + "' cannot inherit from multiple classes");
                continue;
            }
            type.baseClass = inheritedName;
            resolveInheritance(*inherited);
            if (inheritanceState[inherited->name] == 1) continue;
            type.inheritedFieldCount = inherited->fields.size();
            std::vector<std::pair<std::string, DataType>> fields = inherited->fields;
            for (const auto& field : type.fields) {
                const auto duplicate = std::find_if(fields.begin(), fields.end(), [&](const auto& existing) {
                    return existing.first == field.first;
                });
                if (duplicate != fields.end())
                    Error(root, "field '" + field.first + "' conflicts with an inherited field");
                fields.push_back(field);
            }
            type.fields = std::move(fields);
            for (const auto& interfaceName : inherited->interfaces) {
                if (std::find(type.interfaces.begin(), type.interfaces.end(), interfaceName) ==
                    type.interfaces.end()) type.interfaces.push_back(interfaceName);
            }
            for (const auto& method : type.methods) {
                if (method.constructor || method.destructor) continue;
                const FunctionSignature* baseMethod = FindExactMethod(
                    inherited, method.name, method.parameters, method.parameterModes);
                if (baseMethod && (baseMethod->returnType != method.returnType ||
                    baseMethod->parameterModes != method.parameterModes ||
                    baseMethod->returnsReference != method.returnsReference ||
                    baseMethod->returnReferenceConst != method.returnReferenceConst)) {
                    Error(root, "overriding method must preserve the base signature for '" +
                                method.name + "'");
                }
            }
        }
        state = 2;
    };
    for (auto& type : classes_) resolveInheritance(type);

    for (const auto& type : classes_) {
        if (type.interfaceType) continue;
        for (const auto& interfaceName : type.interfaces) {
            const ClassSignature* interfaceType = FindClass(interfaceName);
            if (!interfaceType || !interfaceType->interfaceType) continue;
            for (const auto& required : interfaceType->methods) {
                bool found = false;
                const FunctionSignature* method = FindExactMethod(
                    &type, required.name, required.parameters, required.parameterModes);
                if (method && method->returnType == required.returnType &&
                    method->parameterModes == required.parameterModes &&
                    method->returnsReference == required.returnsReference &&
                    method->returnReferenceConst == required.returnReferenceConst) found = true;
                if (!found) Error(root, "class '" + type.name + "' does not implement " +
                                        interfaceName + "::" + required.Declaration());
            }
        }
    }
    for (AstNode* node : TopLevelDeclarations(root)) {
        if (node->kind != NodeKind::FunctionDecl) continue;
        FunctionSignature signature{node->token.lexeme, node->declaredType, {}, false, {}, {}, false, false, 0, {}, {},
                                    node->returnsReference, node->returnReferenceConst, false};
        for (AstNode* child = node->firstChild; child && child->kind == NodeKind::Parameter;
             child = child->nextSibling) {
            signature.parameters.push_back(child->declaredType);
            signature.parameterNames.push_back(child->token.lexeme);
            signature.parameterModes.push_back(child->parameterMode);
            if (child->firstChild) ++signature.defaultArgumentCount;
        }
        for (const auto& existing : functions_) {
            if (existing.name == signature.name && existing.parameters == signature.parameters) {
                Error(node, "duplicate function '" + signature.Declaration() + "'");
            }
        }
        functions_.push_back(std::move(signature));
    }
}

void TypeChecker::PredeclareGlobals(AstNode* root) {
    globals_.clear();
    if (!root) return;
    std::vector<AstNode*> declarations;
    for (AstNode* node : TopLevelDeclarations(root)) {
        if (node->kind == NodeKind::VarDecl && node->isGlobal) declarations.push_back(node);
        if (node->kind == NodeKind::DeclList) {
            for (AstNode* declaration = node->firstChild; declaration;
                 declaration = declaration->nextSibling) {
                if (declaration->isGlobal) declarations.push_back(declaration);
            }
        }
    }
    for (AstNode* declaration : declarations) {
        if (!declaration->isAuto)
            Declare(declaration->token, declaration->declaredType, declaration->isConst, true);
    }
    for (AstNode* node : TopLevelDeclarations(root)) {
        std::vector<AstNode*> group;
        if (node->kind == NodeKind::VarDecl && node->isGlobal) group.push_back(node);
        if (node->kind == NodeKind::DeclList) {
            for (AstNode* declaration = node->firstChild; declaration;
                 declaration = declaration->nextSibling) {
                if (declaration->isGlobal) group.push_back(declaration);
            }
        }
        DataType sharedAutoType = DataType::Invalid();
        for (AstNode* declaration : group) {
            if (!declaration->isAuto) continue;
            if (!declaration->firstChild) {
                Error(declaration, "auto declaration requires an initializer");
                continue;
            }
            const std::string previousNamespace = currentNamespace_;
            currentNamespace_ = NamespaceOf(declaration->token.lexeme);
            if (!sharedAutoType.IsValid()) sharedAutoType = CheckExpression(declaration->firstChild);
            currentNamespace_ = previousNamespace;
            declaration->declaredType = sharedAutoType;
            Declare(declaration->token, declaration->declaredType, declaration->isConst, true);
        }
    }
    for (AstNode* declaration : declarations) {
        globals_.push_back({declaration->token.lexeme, declaration->declaredType,
                            declaration->isConst, {}});
    }
}

void TypeChecker::CheckNode(AstNode* node) {
    if (!node) return;
    switch (node->kind) {
    case NodeKind::FunctionDecl: CheckFunction(node); break;
    case NodeKind::Block: CheckBlock(node); break;
    case NodeKind::DeclList: {
        DataType sharedAutoType = DataType::Invalid();
        for (AstNode* declaration = node->firstChild; declaration; declaration = declaration->nextSibling) {
            if (declaration->isAuto && sharedAutoType.IsValid()) declaration->declaredType = sharedAutoType;
            CheckNode(declaration);
            if (declaration->isAuto && !sharedAutoType.IsValid()) sharedAutoType = declaration->declaredType;
        }
        break;
    }
    case NodeKind::VarDecl: {
        const std::string previousNamespace = currentNamespace_;
        if (node->isGlobal) currentNamespace_ = NamespaceOf(node->token.lexeme);
        if (node->isAuto && !node->firstChild) {
            Error(node, "auto declaration requires an initializer");
        }
        if (node->firstChild) {
            DataType value = CheckExpression(node->firstChild);
            if (node->isAuto && !node->declaredType.IsValid()) node->declaredType = value;
            if (!CanConvert(value, node->declaredType)) {
                Error(node, "cannot initialize " + node->declaredType.Name() + " with " + value.Name());
            }
        }
        if (!node->isGlobal) Declare(node->token, node->declaredType, node->isConst);
        currentNamespace_ = previousNamespace;
        break;
    }
    case NodeKind::IfStmt: {
        AstNode* condition = node->firstChild;
        if (CheckExpression(condition) != DataType::Bool()) Error(condition, "condition must be bool");
        for (AstNode* branch = condition ? condition->nextSibling : nullptr; branch; branch = branch->nextSibling) {
            CheckNode(branch);
        }
        break;
    }
    case NodeKind::WhileStmt: {
        AstNode* condition = node->firstChild;
        if (CheckExpression(condition) != DataType::Bool()) Error(condition, "condition must be bool");
        ++breakableDepth_;
        ++loopDepth_;
        CheckNode(condition ? condition->nextSibling : nullptr);
        --loopDepth_;
        --breakableDepth_;
        break;
    }
    case NodeKind::ForStmt: {
        const auto children = node->Children();
        scopes_.emplace_back();
        if (children[0]->kind != NodeKind::EmptyStmt) CheckNode(children[0]);
        if (children[1]->kind != NodeKind::EmptyStmt &&
            CheckExpression(children[1]) != DataType::Bool()) Error(children[1], "condition must be bool");
        if (children[2]->kind != NodeKind::EmptyStmt) CheckExpression(children[2]);
        ++breakableDepth_;
        ++loopDepth_;
        CheckNode(children[3]);
        --loopDepth_;
        --breakableDepth_;
        scopes_.pop_back();
        break;
    }
    case NodeKind::DoWhileStmt: {
        const auto children = node->Children();
        ++breakableDepth_;
        ++loopDepth_;
        CheckNode(children[0]);
        --loopDepth_;
        --breakableDepth_;
        if (CheckExpression(children[1]) != DataType::Bool()) Error(children[1], "condition must be bool");
        break;
    }
    case NodeKind::SwitchStmt: {
        AstNode* selector = node->firstChild;
        const DataType selectorType = CheckExpression(selector);
        if (!selectorType.IsInteger()) Error(selector, "switch expression must be an integer");
        std::unordered_set<std::string> values;
        bool hasDefault = false;
        scopes_.emplace_back();
        ++breakableDepth_;
        for (AstNode* clause = selector ? selector->nextSibling : nullptr; clause;
             clause = clause->nextSibling) {
            AstNode* statement = clause->firstChild;
            if (clause->kind == NodeKind::CaseClause) {
                AstNode* valueExpression = statement;
                const DataType valueType = CheckExpression(valueExpression);
                ConstantExpressionEvaluator evaluator([this](std::string_view name) {
                    return FindEnumConstant(name);
                });
                auto value = evaluator.Evaluate(valueExpression);
                if (!valueType.IsInteger() || !value || !value->Type().IsInteger()) {
                    Error(valueExpression, "case value must be an integer constant expression");
                } else if (selectorType.IsInteger() &&
                           !values.insert(ConvertInteger(*value, selectorType).ToString()).second) {
                    Error(valueExpression, "duplicate case value");
                }
                statement = statement ? statement->nextSibling : nullptr;
            } else if (clause->kind == NodeKind::DefaultClause) {
                if (hasDefault) Error(clause, "duplicate default clause");
                hasDefault = true;
            }
            for (; statement; statement = statement->nextSibling) CheckNode(statement);
        }
        --breakableDepth_;
        scopes_.pop_back();
        break;
    }
    case NodeKind::ReturnStmt: {
        DataType value = node->firstChild ? CheckExpression(node->firstChild) : DataType::Void();
        if (currentReturnsReference_) {
            if (!node->firstChild || value != currentReturn_ || !CanReturnReference(node->firstChild))
                Error(node, "return reference must name a global variable or a field with sufficient lifetime");
        } else if (!CanConvert(value, currentReturn_)) {
            Error(node, "cannot return " + value.Name() + " from function returning " + currentReturn_.Name());
        }
        break;
    }
    case NodeKind::BreakStmt:
        if (breakableDepth_ == 0) Error(node, "break statement is not inside a loop or switch");
        break;
    case NodeKind::ContinueStmt:
        if (loopDepth_ == 0) Error(node, "continue statement is not inside a loop");
        break;
    case NodeKind::ExprStmt: CheckExpression(node->firstChild); break;
    case NodeKind::ClassDecl: {
        const ClassSignature* previousClass = currentClass_;
        const std::string previousNamespace = currentNamespace_;
        currentNamespace_ = NamespaceOf(node->token.lexeme);
        currentClass_ = FindClass(node->token.lexeme);
        bool hasConstructor = false;
        for (AstNode* member = node->firstChild; member; member = member->nextSibling) {
            if (member->kind == NodeKind::FieldDecl && member->firstChild) {
                const DataType value = CheckExpression(member->firstChild);
                if (!CanConvert(value, member->declaredType)) {
                    Error(member, "cannot initialize field " + member->declaredType.Name() +
                                  " with " + value.Name());
                }
            }
            if (member->kind == NodeKind::FunctionDecl && member->firstChild) {
                hasConstructor = hasConstructor || member->isConstructor;
                CheckFunction(member);
            }
        }
        if (currentClass_ && !currentClass_->baseClass.empty() && !hasConstructor) {
            const ClassSignature* base = FindClass(currentClass_->baseClass);
            bool hasDeclaredConstructors = false;
            bool hasDefaultConstructor = false;
            if (base) {
                for (const auto& method : base->methods) {
                    if (!method.constructor) continue;
                    hasDeclaredConstructors = true;
                    hasDefaultConstructor = hasDefaultConstructor ||
                        MatchArguments(method, {}, {}).has_value();
                }
            }
            if (hasDeclaredConstructors && !hasDefaultConstructor)
                Error(node, "base class '" + currentClass_->baseClass +
                            "' has no default constructor");
        }
        currentClass_ = previousClass;
        currentNamespace_ = previousNamespace;
        break;
    }
    case NodeKind::InterfaceDecl: case NodeKind::EnumDecl: case NodeKind::EnumValue:
    case NodeKind::TypedefDecl: case NodeKind::NamespaceDecl:
    case NodeKind::EmptyStmt:
    case NodeKind::CaseClause: case NodeKind::DefaultClause: break;
    default: CheckExpression(node); break;
    }
}

void TypeChecker::CheckFunction(AstNode* node) {
    const DataType previousReturn = currentReturn_;
    const bool previousReturnsReference = currentReturnsReference_;
    const std::string previousNamespace = currentNamespace_;
    const bool previousConstructor = currentConstructor_;
    const int previousSuperCallCount = superCallCount_;
    currentNamespace_ = currentClass_ ? NamespaceOf(currentClass_->name)
                                      : NamespaceOf(node->token.lexeme);
    currentReturn_ = node->declaredType;
    currentReturnsReference_ = node->returnsReference;
    currentConstructor_ = node->isConstructor;
    superCallCount_ = 0;
    scopes_.emplace_back();
    AstNode* child = node->firstChild;
    while (child && child->kind == NodeKind::Parameter) {
        if (child->firstChild) {
            if (child->parameterMode == ParameterMode::Out ||
                child->parameterMode == ParameterMode::InOut) {
                Error(child, "out and inout parameters cannot have default arguments");
            }
            const DataType value = CheckExpression(child->firstChild);
            if (!CanConvert(value, child->declaredType)) {
                Error(child, "cannot initialize default argument of type " +
                             child->declaredType.Name() + " with " + value.Name());
            }
        }
        Declare(child->token, child->declaredType, child->parameterMode == ParameterMode::In);
        child = child->nextSibling;
    }
    if (child && child->kind == NodeKind::Block) CheckBlock(child, false);
    if (node->isConstructor) {
        node->hasExplicitSuper = superCallCount_ != 0;
        if (currentClass_ && !currentClass_->baseClass.empty() && superCallCount_ == 0) {
            const ClassSignature* base = FindClass(currentClass_->baseClass);
            bool hasDeclaredConstructors = false;
            bool hasDefaultConstructor = false;
            if (base) {
                for (const auto& method : base->methods) {
                    if (!method.constructor) continue;
                    hasDeclaredConstructors = true;
                    hasDefaultConstructor = hasDefaultConstructor ||
                        MatchArguments(method, {}, {}).has_value();
                }
            }
            if (hasDeclaredConstructors && !hasDefaultConstructor)
                Error(node, "base class '" + currentClass_->baseClass +
                            "' has no default constructor; call super(...) explicitly");
        }
    }
    scopes_.pop_back();
    currentReturn_ = previousReturn;
    currentReturnsReference_ = previousReturnsReference;
    currentConstructor_ = previousConstructor;
    superCallCount_ = previousSuperCallCount;
    currentNamespace_ = previousNamespace;
}

void TypeChecker::CheckBlock(AstNode* node, bool createScope) {
    if (createScope) scopes_.emplace_back();
    for (AstNode* child = node->firstChild; child; child = child->nextSibling) CheckNode(child);
    if (createScope) scopes_.pop_back();
}

DataType TypeChecker::CheckExpression(AstNode* node) {
    if (!node) return DataType::Invalid();
    DataType result = DataType::Invalid();
    switch (node->kind) {
    case NodeKind::Literal:
        switch (node->token.kind) {
        case TokenKind::Integer: case TokenKind::Bits:
        case TokenKind::Float: case TokenKind::Double: {
            const auto value = DecodeNumericLiteral(node->token);
            if (value) result = value->Type();
            else Error(node, "numeric literal is out of range");
            break;
        }
        case TokenKind::String: result = DataType::String(); break;
        case TokenKind::KwTrue: case TokenKind::KwFalse: result = DataType::Bool(); break;
        case TokenKind::KwNull: result = DataType::Object("<null>", true); break;
        default: break;
        }
        break;
    case NodeKind::Identifier: {
        const auto type = Lookup(node->token.lexeme);
        if (type) result = type->type;
        if (!result.IsValid() && currentClass_) {
            for (const auto& field : currentClass_->fields) {
                if (field.first == node->token.lexeme) {
                    result = field.second;
                    node->implicitThis = true;
                    break;
                }
            }
        }
        if (!result.IsValid()) {
            const auto constant = FindEnumConstant(node->token.lexeme);
            if (constant) result = constant->Type();
        }
        if (!result.IsValid()) Error(node, "unknown variable '" + node->token.lexeme + "'");
        break;
    }
    case NodeKind::Member: {
        DataType object = CheckExpression(node->firstChild);
        const ClassSignature* type = FindClass(object.objectName);
        if (!type) Error(node, "unknown object type '" + object.objectName + "'");
        else {
            for (const auto& field : type->fields) if (field.first == node->token.lexeme) result = field.second;
            if (!result.IsValid()) Error(node, "type '" + type->name + "' has no field '" + node->token.lexeme + "'");
        }
        break;
    }
    case NodeKind::Conditional: {
        const auto children = node->Children();
        if (CheckExpression(children[0]) != DataType::Bool())
            Error(children[0], "conditional expression requires a bool condition");
        const DataType whenTrue = CheckExpression(children[1]);
        const DataType whenFalse = CheckExpression(children[2]);
        if (whenTrue.IsNumeric() && whenFalse.IsNumeric()) result = CommonNumericType(whenTrue, whenFalse);
        else if (whenTrue == whenFalse) result = whenTrue;
        else if (CanConvert(whenTrue, whenFalse)) result = whenFalse;
        else if (CanConvert(whenFalse, whenTrue)) result = whenTrue;
        else Error(node, "conditional branches have incompatible types");
        break;
    }
    case NodeKind::Binary: result = CheckBinary(node); break;
    case NodeKind::Unary: result = CheckUnary(node); break;
    case NodeKind::Increment: {
        AstNode* operand = node->firstChild;
        result = CheckExpression(operand);
        if (!operand || (operand->kind != NodeKind::Identifier && operand->kind != NodeKind::Member &&
                         !(operand->kind == NodeKind::Call && operand->returnsReference)))
            Error(operand, "increment operand is not assignable");
        if (IsReadOnlyLValue(operand)) Error(operand, "cannot modify const variable");
        if (!result.IsNumeric()) Error(node, "increment operator requires a numeric operand");
        break;
    }
    case NodeKind::Call: result = CheckCall(node); break;
    case NodeKind::Assign: {
        const auto children = node->Children();
        DataType target = CheckExpression(children[0]);
        if (children[0]->kind != NodeKind::Identifier && children[0]->kind != NodeKind::Member &&
            !(children[0]->kind == NodeKind::Call && children[0]->returnsReference)) {
            Error(children[0], "left side of assignment is not assignable");
        }
        if (children[0]->kind == NodeKind::Identifier && FindEnumConstant(children[0]->token.lexeme)) {
            Error(children[0], "cannot assign to enum value '" + children[0]->token.lexeme + "'");
        } else if (IsReadOnlyLValue(children[0])) {
            Error(children[0], "cannot assign to const variable '" + children[0]->token.lexeme + "'");
        }
        DataType value = CheckExpression(children[1]);
        if (node->token.kind == TokenKind::Equal) {
            if (!CanConvert(value, target))
                Error(node, "cannot assign " + value.Name() + " to " + target.Name());
        } else {
            DataType operationType = DataType::Invalid();
            const bool bitwise = node->token.kind == TokenKind::AmpEqual ||
                node->token.kind == TokenKind::PipeEqual || node->token.kind == TokenKind::CaretEqual ||
                node->token.kind == TokenKind::ShiftLeftEqual ||
                node->token.kind == TokenKind::ShiftRightEqual ||
                node->token.kind == TokenKind::ShiftRightArithmeticEqual;
            if (bitwise) {
                if (!target.IsInteger() || !value.IsInteger())
                    Error(node, "bitwise compound assignment requires integer operands");
                else operationType = target;
            } else if (node->token.kind == TokenKind::PlusEqual && target == DataType::String()) {
                operationType = DataType::String();
            } else if (target.IsNumeric() && value.IsNumeric()) {
                if (node->token.kind == TokenKind::PercentEqual &&
                    (!target.IsInteger() || !value.IsInteger())) {
                    Error(node, "'%=' requires integer operands");
                }
                operationType = CommonNumericType(target, value);
            } else {
                Error(node, "compound assignment requires compatible numeric operands");
            }
            if (operationType.IsValid() && !CanConvert(operationType, target))
                Error(node, "compound assignment result cannot convert to " + target.Name());
        }
        result = target;
        break;
    }
    default: Error(node, "expression is not supported by the type checker"); break;
    }
    node->inferredType = result;
    return result;
}

DataType TypeChecker::CheckBinary(AstNode* node) {
    const auto children = node->Children();
    const DataType left = CheckExpression(children[0]);
    const DataType right = CheckExpression(children[1]);
    const auto op = node->token.kind;
    if (op == TokenKind::Plus && (left == DataType::String() || right == DataType::String())) {
        return DataType::String();
    }
    if (op == TokenKind::AndAnd || op == TokenKind::OrOr) {
        if (left != DataType::Bool() || right != DataType::Bool()) Error(node, "logical operator requires bool operands");
        return DataType::Bool();
    }
    if (op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret ||
        op == TokenKind::ShiftLeft || op == TokenKind::ShiftRight ||
        op == TokenKind::ShiftRightArithmetic) {
        if (!left.IsInteger() || !right.IsInteger())
            Error(node, "bitwise operator requires integer operands");
        return left.IsInteger() ? left : DataType::Invalid();
    }
    if (op == TokenKind::EqualEqual || op == TokenKind::BangEqual || op == TokenKind::KwIs) {
        const bool relatedObjects = left.kind == TypeKind::Object && right.kind == TypeKind::Object &&
            (CanConvert(left, right) || CanConvert(right, left));
        if (left != right && !(left.IsNumeric() && right.IsNumeric()) && !relatedObjects)
            Error(node, "incomparable operand types");
        return DataType::Bool();
    }
    if (!left.IsNumeric() || !right.IsNumeric()) {
        Error(node, "operator requires numeric operands"); return DataType::Invalid();
    }
    if (op == TokenKind::Less || op == TokenKind::LessEqual ||
        op == TokenKind::Greater || op == TokenKind::GreaterEqual) return DataType::Bool();
    return CommonNumericType(left, right);
}

DataType TypeChecker::CheckUnary(AstNode* node) {
    DataType operand = CheckExpression(node->firstChild);
    if (node->token.kind == TokenKind::Bang) {
        if (operand != DataType::Bool()) Error(node, "'!' requires bool operand");
        return DataType::Bool();
    }
    if (node->token.kind == TokenKind::Tilde) {
        if (!operand.IsInteger()) Error(node, "'~' requires an integer operand");
        return operand;
    }
    if (node->token.kind == TokenKind::At) return operand;
    if (!operand.IsNumeric()) Error(node, "numeric unary operator requires numeric operand");
    return operand;
}

DataType TypeChecker::CheckCall(AstNode* node) {
    AstNode* callee = node->firstChild;
    if (!callee) return DataType::Invalid();
    std::vector<DataType> arguments;
    std::vector<AstNode*> argumentNodes;
    std::vector<std::string> argumentNames;
    for (AstNode* argument = callee->nextSibling; argument; argument = argument->nextSibling) {
        AstNode* expression = argument->kind == NodeKind::NamedArgument ? argument->firstChild : argument;
        const DataType type = CheckExpression(expression);
        argument->inferredType = type;
        arguments.push_back(type);
        argumentNodes.push_back(expression);
        argumentNames.push_back(argument->kind == NodeKind::NamedArgument ? argument->token.lexeme
                                                                          : std::string{});
    }
    if (callee->kind == NodeKind::Identifier && callee->token.lexeme == "super") {
        if (!currentClass_ || !currentConstructor_ || currentClass_->baseClass.empty()) {
            Error(node, "super(...) is only valid in a derived class constructor");
            return DataType::Invalid();
        }
        ++superCallCount_;
        if (superCallCount_ > 1) Error(node, "base constructor can only be called once");
        const ClassSignature* base = FindClass(currentClass_->baseClass);
        const FunctionSignature* constructor = nullptr;
        bool hasConstructors = false;
        int bestCost = 1000000;
        if (base) {
            for (const auto& candidate : base->methods) {
                if (!candidate.constructor) continue;
                hasConstructors = true;
                const auto cost = MatchArguments(candidate, arguments, argumentNames);
                if (cost && *cost < bestCost) { constructor = &candidate; bestCost = *cost; }
            }
        }
        if ((hasConstructors && !constructor) || (!hasConstructors && !arguments.empty()))
            Error(node, "no matching base constructor for '" + currentClass_->baseClass + "'");
        else if (constructor)
            ValidateReferenceArguments(*constructor, argumentNodes, argumentNames);
        node->nonVirtualCall = true;
        return DataType::Void();
    }
    if (callee->kind == NodeKind::Identifier) {
        if (const ClassSignature* type = FindClass(callee->token.lexeme)) {
            if (type->interfaceType) {
                Error(node, "interface types cannot be constructed");
                return DataType::Invalid();
            }
            const FunctionSignature* constructor = nullptr;
            int bestCost = 1000000;
            bool hasConstructors = false;
            for (const auto& candidate : type->methods) {
                if (!candidate.constructor) continue;
                hasConstructors = true;
                const auto cost = MatchArguments(candidate, arguments, argumentNames);
                if (cost && *cost < bestCost) { constructor = &candidate; bestCost = *cost; }
            }
            if ((hasConstructors && !constructor) || (!hasConstructors && !arguments.empty())) {
                Error(node, "no matching constructor for '" + type->name + "'");
                return DataType::Invalid();
            }
            if (constructor)
                ValidateReferenceArguments(*constructor, argumentNodes, argumentNames);
            return DataType::Object(type->name, true);
        }
    }
    if (callee->kind == NodeKind::Member) {
        const DataType object = CheckExpression(callee->firstChild);
        const FunctionSignature* method = FindMethod(object, callee->token.lexeme, arguments, argumentNames);
        if (!method) Error(node, "no matching method for '" + callee->token.lexeme + "'");
        else {
            ValidateReferenceArguments(*method, argumentNodes, argumentNames);
            node->returnsReference = method->returnsReference;
            node->returnReferenceConst = method->returnReferenceConst;
        }
        return method ? method->returnType : DataType::Invalid();
    }
    if (callee->kind != NodeKind::Identifier) {
        Error(node, "callee is not callable"); return DataType::Invalid();
    }
    const auto scope = callee->token.lexeme.rfind("::");
    if (currentClass_ && scope != std::string::npos) {
        const std::string ownerName = callee->token.lexeme.substr(0, scope);
        const std::string methodName = callee->token.lexeme.substr(scope + 2);
        const ClassSignature* owner = FindClass(ownerName);
        if (owner && !owner->interfaceType && IsDerivedFrom(currentClass_->name, owner->name)) {
            const FunctionSignature* method = FindMethodInClass(owner, methodName, arguments, argumentNames);
            if (!method) Error(node, "no matching base method for '" + callee->token.lexeme + "'");
            else {
                ValidateReferenceArguments(*method, argumentNodes, argumentNames);
                node->returnsReference = method->returnsReference;
                node->returnReferenceConst = method->returnReferenceConst;
                node->nonVirtualCall = true;
            }
            return method ? method->returnType : DataType::Invalid();
        }
    }
    if (currentClass_) {
        const FunctionSignature* method = FindMethod(
            DataType::Object(currentClass_->name, true), callee->token.lexeme, arguments, argumentNames);
        if (method) {
            ValidateReferenceArguments(*method, argumentNodes, argumentNames);
            node->returnsReference = method->returnsReference;
            node->returnReferenceConst = method->returnReferenceConst;
            return method->returnType;
        }
    }
    const FunctionSignature* best = nullptr;
    int bestCost = 1000000;
    for (const auto& function : functions_) {
        const auto nameCost = NameMatchCost(function.name, callee->token.lexeme, currentNamespace_);
        const auto argumentCost = MatchArguments(function, arguments, argumentNames);
        if (!nameCost || !argumentCost) continue;
        const int cost = *nameCost + *argumentCost;
        if (cost < bestCost) { best = &function; bestCost = cost; }
    }
    if (!best) {
        Error(node, "no matching function for '" + callee->token.lexeme + "'");
        return DataType::Invalid();
    }
    ValidateReferenceArguments(*best, argumentNodes, argumentNames);
    node->returnsReference = best->returnsReference;
    node->returnReferenceConst = best->returnReferenceConst;
    return best->returnType;
}

std::optional<TypeChecker::VariableSymbol> TypeChecker::Lookup(std::string_view name) const {
    for (const auto& candidate : NameCandidates(currentNamespace_, name)) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(candidate);
            if (found != scope->end()) return found->second;
        }
    }
    return std::nullopt;
}

std::optional<Value> TypeChecker::FindEnumConstant(std::string_view name) const {
    for (const auto& candidate : NameCandidates(currentNamespace_, name)) {
        const auto found = enumConstants_.find(candidate);
        if (found != enumConstants_.end()) return found->second;
    }
    return std::nullopt;
}

const FunctionSignature* TypeChecker::FindMethod(
    const DataType& object, std::string_view name, const std::vector<DataType>& arguments,
    const std::vector<std::string>& argumentNames) const {
    const ClassSignature* type = FindClass(object.objectName);
    return FindMethodInClass(type, name, arguments, argumentNames);
}

const FunctionSignature* TypeChecker::FindMethodInClass(
    const ClassSignature* type, std::string_view name, const std::vector<DataType>& arguments,
    const std::vector<std::string>& argumentNames) const {
    if (!type) return nullptr;
    const FunctionSignature* best = nullptr;
    int bestCost = 1000000;
    for (const auto& method : type->methods) {
        if (method.constructor || method.destructor || method.name != name) continue;
        const auto cost = MatchArguments(method, arguments, argumentNames);
        if (cost && *cost < bestCost) { best = &method; bestCost = *cost; }
    }
    if (best || type->baseClass.empty()) return best;
    return FindMethodInClass(FindClass(type->baseClass), name, arguments, argumentNames);
}

const FunctionSignature* TypeChecker::FindExactMethod(
    const ClassSignature* type, std::string_view name, const std::vector<DataType>& parameters,
    const std::vector<ParameterMode>& modes) const {
    if (!type) return nullptr;
    for (const auto& method : type->methods) {
        if (!method.constructor && !method.destructor && method.name == name &&
            method.parameters == parameters && method.parameterModes == modes) return &method;
    }
    return type->baseClass.empty() ? nullptr
        : FindExactMethod(FindClass(type->baseClass), name, parameters, modes);
}

std::optional<int> TypeChecker::MatchArguments(
    const FunctionSignature& signature, const std::vector<DataType>& arguments,
    const std::vector<std::string>& argumentNames) const {
    if (arguments.size() != argumentNames.size() || arguments.size() > signature.parameters.size())
        return std::nullopt;
    std::vector<bool> assigned(signature.parameters.size(), false);
    int cost = 0;
    std::size_t positional = 0;
    for (std::size_t argument = 0; argument < arguments.size(); ++argument) {
        std::size_t parameter = positional;
        if (!argumentNames[argument].empty()) {
            parameter = signature.parameterNames.size();
            for (std::size_t index = 0; index < signature.parameterNames.size(); ++index) {
                if (signature.parameterNames[index] == argumentNames[argument]) { parameter = index; break; }
            }
            if (parameter >= signature.parameters.size()) return std::nullopt;
        } else {
            while (parameter < assigned.size() && assigned[parameter]) ++parameter;
            positional = parameter + 1;
        }
        if (parameter >= assigned.size() || assigned[parameter]) return std::nullopt;
        const ParameterMode mode = parameter < signature.parameterModes.size()
            ? signature.parameterModes[parameter] : ParameterMode::Value;
        const auto conversion = (mode == ParameterMode::Out || mode == ParameterMode::InOut)
            ? (arguments[argument] == signature.parameters[parameter]
                   ? std::optional<int>{0} : std::nullopt)
            : ConversionCost(arguments[argument], signature.parameters[parameter]);
        if (!conversion) return std::nullopt;
        assigned[parameter] = true;
        cost += *conversion;
    }
    const std::size_t firstDefault = signature.parameters.size() - signature.defaultArgumentCount;
    for (std::size_t index = 0; index < assigned.size(); ++index)
        if (!assigned[index] && index < firstDefault) return std::nullopt;
    return cost;
}

bool TypeChecker::ValidateReferenceArguments(
    const FunctionSignature& signature, const std::vector<AstNode*>& arguments,
    const std::vector<std::string>& argumentNames) {
    std::vector<AstNode*> ordered(signature.parameters.size(), nullptr);
    std::size_t positional = 0;
    for (std::size_t argument = 0; argument < arguments.size(); ++argument) {
        std::size_t parameter = positional;
        if (!argumentNames[argument].empty()) {
            parameter = signature.parameterNames.size();
            for (std::size_t index = 0; index < signature.parameterNames.size(); ++index) {
                if (signature.parameterNames[index] == argumentNames[argument]) {
                    parameter = index;
                    break;
                }
            }
        } else {
            while (parameter < ordered.size() && ordered[parameter]) ++parameter;
            positional = parameter + 1;
        }
        if (parameter < ordered.size()) ordered[parameter] = arguments[argument];
    }
    bool valid = true;
    for (std::size_t parameter = 0; parameter < ordered.size(); ++parameter) {
        const ParameterMode mode = parameter < signature.parameterModes.size()
            ? signature.parameterModes[parameter] : ParameterMode::Value;
        if (mode != ParameterMode::Out && mode != ParameterMode::InOut) continue;
        AstNode* argument = ordered[parameter];
        if (!argument) continue;
        if (argument->kind != NodeKind::Identifier && argument->kind != NodeKind::Member) {
            Error(argument, "out and inout arguments must be assignable lvalues");
            valid = false;
        } else if (IsReadOnlyLValue(argument)) {
            Error(argument, "const value cannot be passed to out or inout parameter");
            valid = false;
        }
    }
    return valid;
}

bool TypeChecker::IsReadOnlyLValue(const AstNode* node) const {
    if (!node) return false;
    if (node->kind == NodeKind::Identifier) {
        const auto symbol = Lookup(node->token.lexeme);
        return symbol && symbol->isConst;
    }
    if (node->kind == NodeKind::Member) return IsReadOnlyLValue(node->firstChild);
    if (node->kind == NodeKind::Call && node->returnsReference)
        return node->returnReferenceConst;
    return false;
}

bool TypeChecker::CanReturnReference(const AstNode* node) const {
    if (!node) return false;
    if (node->kind == NodeKind::Identifier) {
        if (node->implicitThis) return true;
        const auto symbol = Lookup(node->token.lexeme);
        return symbol && symbol->returnableReference;
    }
    if (node->kind == NodeKind::Member) return CanReturnReference(node->firstChild);
    if (node->kind == NodeKind::Call) return node->returnsReference;
    return false;
}

const ClassSignature* TypeChecker::FindClass(std::string_view name) const {
    for (const auto& candidate : NameCandidates(currentNamespace_, name))
        for (const auto& type : classes_) if (type.name == candidate) return &type;
    return nullptr;
}

bool TypeChecker::IsDerivedFrom(std::string_view derived, std::string_view base) const {
    const ClassSignature* type = FindClass(derived);
    while (type) {
        if (type->name == base) return true;
        type = type->baseClass.empty() ? nullptr : FindClass(type->baseClass);
    }
    return false;
}

void TypeChecker::Declare(const Token& name, const DataType& type, bool isConst,
                          bool returnableReference) {
    auto& scope = scopes_.back();
    if (scope.find(name.lexeme) != scope.end()) {
        diagnostics_.Report(name.location, Severity::Error, "duplicate variable '" + name.lexeme + "'");
    } else scope.emplace(name.lexeme, VariableSymbol{type, isConst, returnableReference});
}

bool TypeChecker::CanConvert(const DataType& from, const DataType& to) const {
    if (from == to) return true;
    if (to.kind == TypeKind::Enum) return false;
    if (from.IsInteger() && (to.IsInteger() || to == DataType::Float() || to == DataType::Double()))
        return true;
    if ((from == DataType::Float() && to == DataType::Double()) ||
        (from == DataType::Double() && to == DataType::Float())) return true;
    if (from.kind == TypeKind::Object && from.objectName == "<null>" && to.isHandle) return true;
    if (from.kind == TypeKind::Object && to.kind == TypeKind::Object && from.isHandle && to.isHandle) {
        if (const auto* type = FindClass(from.objectName)) {
            for (const auto& interfaceName : type->interfaces) if (interfaceName == to.objectName) return true;
            if (IsDerivedFrom(type->name, to.objectName)) return true;
        }
    }
    return false;
}

std::optional<int> TypeChecker::ConversionCost(const DataType& from, const DataType& to) const {
    if (from == to) return 0;
    if (to.kind == TypeKind::Enum) return std::nullopt;
    if (from.IsInteger() && to.IsInteger()) {
        const int widthCost = static_cast<int>(from.IntegerBits() > to.IntegerBits()
            ? from.IntegerBits() - to.IntegerBits() : to.IntegerBits() - from.IntegerBits());
        return 1 + widthCost + (from.IsSignedInteger() != to.IsSignedInteger() ? 1 : 0);
    }
    if (from.IsInteger() && to == DataType::Float()) return 100;
    if (from.IsInteger() && to == DataType::Double()) return 101;
    if (from == DataType::Float() && to == DataType::Double()) return 1;
    if (from == DataType::Double() && to == DataType::Float()) return 2;
    return CanConvert(from, to) ? std::optional<int>{1} : std::nullopt;
}

void TypeChecker::Error(const AstNode* node, std::string message) {
    diagnostics_.Report(node ? node->token.location : SourceLocation{}, Severity::Error, std::move(message));
}

} // namespace mini_as
