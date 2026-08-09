#include "mini_as/type_checker.hpp"
#include "mini_as/constant_evaluator.hpp"

#include <algorithm>
#include <limits>
#include <functional>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace mini_as {
namespace {

bool IsReturnTypeOverload(std::string_view name) {
    return name == "opConv" || name == "opImplConv" ||
           name == "opCast" || name == "opImplCast";
}

bool IsWeakRef(const DataType& type) {
    return type.kind == TypeKind::WeakRef || type.kind == TypeKind::ConstWeakRef;
}

bool SameCallableSignature(const FunctionSignature& function,
                           const FunctionSignature& funcdef) {
    return function.returnType == funcdef.returnType &&
           function.parameters == funcdef.parameters &&
           function.parameterModes == funcdef.parameterModes &&
           function.returnsReference == funcdef.returnsReference &&
           function.returnReferenceConst == funcdef.returnReferenceConst;
}

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

struct FuncdefDeclaration {
    AstNode* node = nullptr;
    std::string parentType;
};

void CollectFuncdefDeclarations(AstNode* owner, std::vector<FuncdefDeclaration>& result) {
    if (!owner) return;
    for (AstNode* node = owner->firstChild; node; node = node->nextSibling) {
        if (node->kind == NodeKind::NamespaceDecl) {
            CollectFuncdefDeclarations(node, result);
        } else if (node->kind == NodeKind::FuncdefDecl) {
            result.push_back({node, {}});
        } else if (node->kind == NodeKind::ClassDecl || node->kind == NodeKind::InterfaceDecl) {
            for (AstNode* member = node->firstChild; member; member = member->nextSibling) {
                if (member->kind == NodeKind::FuncdefDecl)
                    result.push_back({member, node->token.lexeme});
            }
        }
    }
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

std::pair<std::string_view, std::string_view> BinaryOperatorMethods(TokenKind operation) {
    switch (operation) {
    case TokenKind::Plus: return {"opAdd", "opAdd_r"};
    case TokenKind::Minus: return {"opSub", "opSub_r"};
    case TokenKind::Star: return {"opMul", "opMul_r"};
    case TokenKind::Slash: return {"opDiv", "opDiv_r"};
    case TokenKind::Percent: return {"opMod", "opMod_r"};
    case TokenKind::StarStar: return {"opPow", "opPow_r"};
    case TokenKind::Amp: return {"opAnd", "opAnd_r"};
    case TokenKind::Pipe: return {"opOr", "opOr_r"};
    case TokenKind::Caret: return {"opXor", "opXor_r"};
    case TokenKind::ShiftLeft: return {"opShl", "opShl_r"};
    case TokenKind::ShiftRight: return {"opShr", "opShr_r"};
    case TokenKind::ShiftRightArithmetic: return {"opUShr", "opUShr_r"};
    default: return {};
    }
}

std::string_view AssignmentOperatorMethod(TokenKind operation) {
    switch (operation) {
    case TokenKind::Equal: return "opAssign";
    case TokenKind::PlusEqual: return "opAddAssign";
    case TokenKind::MinusEqual: return "opSubAssign";
    case TokenKind::StarEqual: return "opMulAssign";
    case TokenKind::SlashEqual: return "opDivAssign";
    case TokenKind::PercentEqual: return "opModAssign";
    case TokenKind::StarStarEqual: return "opPowAssign";
    case TokenKind::AmpEqual: return "opAndAssign";
    case TokenKind::PipeEqual: return "opOrAssign";
    case TokenKind::CaretEqual: return "opXorAssign";
    case TokenKind::ShiftLeftEqual: return "opShlAssign";
    case TokenKind::ShiftRightEqual: return "opShrAssign";
    case TokenKind::ShiftRightArithmeticEqual: return "opUShrAssign";
    default: return {};
    }
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
    out << ')';
    if (readOnlyMethod) out << " const";
    return out.str();
}

TypeChecker::TypeChecker(DiagnosticSink& diagnostics) : diagnostics_(diagnostics) {}

void TypeChecker::RegisterFunction(FunctionSignature signature) {
    functions_.push_back(std::move(signature));
}

void TypeChecker::RegisterGlobalProperty(GlobalSignature signature) {
    registeredGlobals_.push_back(std::move(signature));
}

void TypeChecker::RegisterObjectType(ClassSignature signature) {
    registeredClasses_.push_back(std::move(signature));
}

bool TypeChecker::Check(AstNode* root) {
    scopes_.clear();
    activeLambdas_.clear();
    scopes_.emplace_back();
    PredeclareTypedefs(root);
    PredeclareEnums(root);
    PredeclareFuncdefs(root);
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
const std::vector<FuncdefSignature>& TypeChecker::Funcdefs() const { return funcdefs_; }

void TypeChecker::PredeclareFuncdefs(AstNode* root) {
    funcdefs_.clear();
    if (!root) return;
    std::vector<FuncdefDeclaration> declarations;
    CollectFuncdefDeclarations(root, declarations);
    for (const auto& declaration : declarations) {
        AstNode* node = declaration.node;
        bool duplicate = false;
        for (const auto& existing : funcdefs_)
            duplicate = duplicate || existing.name == node->token.lexeme;
        for (AstNode* other : TopLevelDeclarations(root)) {
            if (other == node) continue;
            if ((other->kind == NodeKind::ClassDecl || other->kind == NodeKind::InterfaceDecl ||
                 other->kind == NodeKind::EnumDecl || other->kind == NodeKind::TypedefDecl) &&
                other->token.lexeme == node->token.lexeme) duplicate = true;
        }
        if (duplicate) {
            Error(node, "duplicate or conflicting funcdef '" + node->token.lexeme + "'");
            continue;
        }
        FunctionSignature signature{node->token.lexeme, node->declaredType, {}, false, {}, {}, false,
                                    false, 0, {}, {}, node->returnsReference,
                                    node->returnReferenceConst, false};
        for (AstNode* parameter = node->firstChild; parameter; parameter = parameter->nextSibling) {
            signature.parameters.push_back(parameter->declaredType);
            signature.parameterNames.push_back(parameter->token.lexeme);
            signature.parameterModes.push_back(parameter->parameterMode);
            if (parameter->firstChild)
                Error(parameter, "funcdef parameters cannot have default arguments");
        }
        funcdefs_.push_back({node->token.lexeme, std::move(signature), {},
                            declaration.parentType});
    }
}

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
    classes_ = registeredClasses_;
    if (!root) return;
    for (AstNode* node : TopLevelDeclarations(root)) {
        if (node->kind != NodeKind::ClassDecl && node->kind != NodeKind::InterfaceDecl) continue;
        const auto duplicateType = std::find_if(classes_.begin(), classes_.end(), [&](const auto& type) {
            return type.name == node->token.lexeme;
        });
        if (duplicateType != classes_.end()) {
            Error(node, "duplicate type '" + node->token.lexeme + "'");
            continue;
        }
        ClassSignature type;
        type.name = node->token.lexeme;
        type.interfaceType = node->kind == NodeKind::InterfaceDecl;
        AstNode* child = node->firstChild;
        while (child && child->kind == NodeKind::Identifier) {
            type.inheritedTypes.push_back(child->token.lexeme);
            child = child->nextSibling;
        }
        for (; child; child = child->nextSibling) {
            if (child->kind == NodeKind::FieldDecl) {
                if (child->declaredType.kind == TypeKind::Function && !child->declaredType.isHandle)
                    Error(child, "funcdef fields must be declared as handles");
                type.fields.push_back({child->token.lexeme, child->declaredType,
                                       type.name, child->memberAccess});
            }
            else if (child->kind == NodeKind::FunctionDecl) {
                FunctionSignature method{child->token.lexeme, child->declaredType, {}, false, {},
                                         type.name, true, child->isConstructor, 0, {}, {},
                                         child->returnsReference, child->returnReferenceConst,
                                         child->isDestructor};
                method.access = child->memberAccess;
                method.propertyAccessor = child->propertyAccessor;
                for (AstNode* parameter = child->firstChild;
                     parameter && parameter->kind == NodeKind::Parameter; parameter = parameter->nextSibling) {
                    method.parameters.push_back(parameter->declaredType);
                    method.parameterNames.push_back(parameter->token.lexeme);
                    method.parameterModes.push_back(parameter->parameterMode);
                    if (parameter->firstChild) ++method.defaultArgumentCount;
                }
                AstNode* body = child->firstChild;
                while (body && body->kind == NodeKind::Parameter) body = body->nextSibling;
                if (child->isDeleted) {
                    if (type.interfaceType) {
                        Error(child, "interface methods cannot be deleted");
                        if (body && body->kind == NodeKind::Block)
                            Error(child, "deleted function cannot have an implementation");
                        continue;
                    }
                    const bool sameClassParameter = method.parameters.size() == 1 &&
                        method.parameters[0].kind == TypeKind::Object &&
                        method.parameters[0].objectName == type.name;
                    if (method.constructor && method.parameters.empty()) {
                        if (type.defaultConstructorDeleted)
                            Error(child, "default constructor is already deleted");
                        type.defaultConstructorDeleted = true;
                    } else if (method.constructor && sameClassParameter) {
                        if (type.defaultCopyConstructorDeleted)
                            Error(child, "default copy constructor is already deleted");
                        type.defaultCopyConstructorDeleted = true;
                    } else if (!method.constructor && method.name == "opAssign" &&
                               sameClassParameter) {
                        if (type.defaultCopyAssignmentDeleted)
                            Error(child, "default copy assignment is already deleted");
                        type.defaultCopyAssignmentDeleted = true;
                    } else {
                        Error(child, "only default construction, copy construction, and copy assignment can be deleted");
                    }
                    if (body && body->kind == NodeKind::Block)
                        Error(child, "deleted function cannot have an implementation");
                    continue;
                }
                bool duplicate = false;
                for (const auto& existing : type.methods) {
                    if (existing.name == method.name && existing.parameters == method.parameters &&
                        existing.constructor == method.constructor &&
                        existing.destructor == method.destructor &&
                        (!IsReturnTypeOverload(method.name) ||
                         existing.returnType == method.returnType)) duplicate = true;
                }
                if (duplicate) Error(child, "duplicate method or constructor '" + method.Declaration() + "'");
                if (method.destructor && !method.parameters.empty())
                    Error(child, "destructor cannot declare parameters");
                if (method.destructor && (!body || body->kind != NodeKind::Block))
                    Error(child, "destructor must have a body");
                type.methods.push_back(std::move(method));
            }
        }
        const auto sameClassParameter = [&type](const FunctionSignature& method) {
            return method.parameters.size() == 1 && method.parameters[0].kind == TypeKind::Object &&
                method.parameters[0].objectName == type.name;
        };
        for (const auto& method : type.methods) {
            if (type.defaultConstructorDeleted && method.constructor && method.parameters.empty())
                Error(node, "cannot define a default constructor that is deleted");
            if (type.defaultCopyConstructorDeleted && method.constructor && sameClassParameter(method))
                Error(node, "cannot define a copy constructor that is deleted");
            if (type.defaultCopyAssignmentDeleted && method.name == "opAssign" &&
                sameClassParameter(method))
                Error(node, "cannot define a copy assignment that is deleted");
        }
        type.generatedCopyConstructor = !type.interfaceType &&
            !type.defaultCopyConstructorDeleted &&
            std::none_of(type.methods.begin(), type.methods.end(), [](const auto& method) {
                return method.constructor && method.parameters.size() == 1;
            });
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
            std::vector<FieldSignature> fields = inherited->fields;
            for (const auto& field : type.fields) {
                const auto duplicate = std::find_if(fields.begin(), fields.end(), [&](const auto& existing) {
                    return existing.name == field.name;
                });
                if (duplicate != fields.end())
                    Error(root, "field '" + field.name + "' conflicts with an inherited field");
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
                if (baseMethod && !IsReturnTypeOverload(method.name) &&
                    (baseMethod->returnType != method.returnType ||
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
        for (const auto& method : type.methods) {
            if (!method.propertyAccessor) continue;
            const bool getter = method.name.rfind("get_", 0) == 0;
            const bool setter = method.name.rfind("set_", 0) == 0;
            if ((!getter && !setter) || method.name.size() <= 4) {
                Error(root, "property accessor must be named get_<name> or set_<name>");
                continue;
            }
            if ((getter && (method.returnType == DataType::Void() || !method.parameters.empty())) ||
                (setter && (method.returnType != DataType::Void() || method.parameters.size() != 1))) {
                Error(root, "indexed or malformed property accessor '" + method.Declaration() + "'");
                continue;
            }
            if (!getter) continue;
            const std::string setterName = "set_" + method.name.substr(4);
            for (const auto& candidate : type.methods) {
                if (!candidate.propertyAccessor || candidate.name != setterName ||
                    candidate.parameters.size() != 1) continue;
                if (candidate.parameters[0] != method.returnType)
                    Error(root, "property getter and setter types must match for '" +
                                method.name.substr(4) + "'");
            }
        }
    }

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
        if (node->isDeleted) {
            Error(node, "only class default operations can be deleted");
            AstNode* body = node->firstChild;
            while (body && body->kind == NodeKind::Parameter) body = body->nextSibling;
            if (body && body->kind == NodeKind::Block)
                Error(node, "deleted function cannot have an implementation");
            continue;
        }
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
            if (existing.factory) continue;
            if (existing.name == signature.name && existing.parameters == signature.parameters) {
                Error(node, "duplicate function '" + signature.Declaration() + "'");
            }
        }
        functions_.push_back(std::move(signature));
    }
}

void TypeChecker::PredeclareGlobals(AstNode* root) {
    globals_ = registeredGlobals_;
    for (const auto& property : registeredGlobals_)
        Declare(Token{TokenKind::Identifier, property.name, {"registration"}},
                property.type, property.isConst, true);
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
                            declaration->isConst, {}, false});
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
        if (!node->isAuto && node->declaredType.kind == TypeKind::Function &&
            !node->declaredType.isHandle)
            Error(node, "funcdef variables must be declared as handles");
        if (!node->isAuto && IsWeakRef(node->declaredType) &&
            !FindClass(node->declaredType.objectName))
            Error(node, "weakref subtype must name a script class");
        if (node->firstChild) {
            DataType value = CheckExpression(node->firstChild,
                                             node->isAuto ? std::nullopt
                                                          : std::optional<DataType>{node->declaredType});
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
        DataType value = node->firstChild
            ? CheckExpression(node->firstChild, currentReturn_) : DataType::Void();
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
    case NodeKind::TryStmt:
        CheckNode(node->firstChild);
        CheckNode(node->firstChild ? node->firstChild->nextSibling : nullptr);
        break;
    case NodeKind::ExprStmt: CheckExpression(node->firstChild); break;
    case NodeKind::ClassDecl: {
        const ClassSignature* previousClass = currentClass_;
        const std::string previousNamespace = currentNamespace_;
        currentNamespace_ = NamespaceOf(node->token.lexeme);
        currentClass_ = FindClass(node->token.lexeme);
        bool hasConstructor = currentClass_ && currentClass_->defaultConstructorDeleted;
        for (AstNode* member = node->firstChild; member; member = member->nextSibling) {
            if (member->kind == NodeKind::FieldDecl && member->firstChild) {
                const DataType value = CheckExpression(member->firstChild);
                if (!CanConvert(value, member->declaredType)) {
                    Error(member, "cannot initialize field " + member->declaredType.Name() +
                                  " with " + value.Name());
                }
            }
            if (member->kind == NodeKind::FieldDecl && IsWeakRef(member->declaredType) &&
                !FindClass(member->declaredType.objectName))
                Error(member, "weakref subtype must name a script class");
            if (member->kind == NodeKind::FunctionDecl && member->firstChild && !member->isDeleted) {
                hasConstructor = hasConstructor || member->isConstructor;
                CheckFunction(member);
            }
        }
        if (currentClass_ && !currentClass_->baseClass.empty() && !hasConstructor) {
            const ClassSignature* base = FindClass(currentClass_->baseClass);
            bool hasDeclaredConstructors = false;
            const FunctionSignature* defaultConstructor = nullptr;
            if (base) {
                for (const auto& method : base->methods) {
                    if (!method.constructor) continue;
                    hasDeclaredConstructors = true;
                    if (MatchArguments(method, {}, {})) defaultConstructor = &method;
                }
            }
            if (base && base->defaultConstructorDeleted && !defaultConstructor)
                Error(node, "default constructor for base class '" + currentClass_->baseClass +
                            "' is deleted");
            else if (hasDeclaredConstructors && !defaultConstructor)
                Error(node, "base class '" + currentClass_->baseClass +
                            "' has no default constructor");
            else if (defaultConstructor)
                CheckAccess(node, defaultConstructor->access, defaultConstructor->objectType,
                            "constructor", defaultConstructor->name);
        }
        currentClass_ = previousClass;
        currentNamespace_ = previousNamespace;
        break;
    }
    case NodeKind::InterfaceDecl: case NodeKind::EnumDecl: case NodeKind::EnumValue:
    case NodeKind::TypedefDecl: case NodeKind::FuncdefDecl: case NodeKind::NamespaceDecl:
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
    if (IsWeakRef(currentReturn_) && !FindClass(currentReturn_.objectName))
        Error(node, "weakref subtype must name a script class");
    currentReturnsReference_ = node->returnsReference;
    currentConstructor_ = node->isConstructor;
    superCallCount_ = 0;
    scopes_.emplace_back();
    AstNode* child = node->firstChild;
    while (child && child->kind == NodeKind::Parameter) {
        if (child->declaredType.kind == TypeKind::Function && !child->declaredType.isHandle)
            Error(child, "funcdef parameters must be declared as handles");
        if (IsWeakRef(child->declaredType) && !FindClass(child->declaredType.objectName))
            Error(child, "weakref subtype must name a script class");
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
            const FunctionSignature* defaultConstructor = nullptr;
            if (base) {
                for (const auto& method : base->methods) {
                    if (!method.constructor) continue;
                    hasDeclaredConstructors = true;
                    if (MatchArguments(method, {}, {})) defaultConstructor = &method;
                }
            }
            if (base && base->defaultConstructorDeleted && !defaultConstructor)
                Error(node, "default constructor for base class '" + currentClass_->baseClass +
                            "' is deleted; call super(...) explicitly");
            else if (hasDeclaredConstructors && !defaultConstructor)
                Error(node, "base class '" + currentClass_->baseClass +
                            "' has no default constructor; call super(...) explicitly");
            else if (defaultConstructor)
                CheckAccess(node, defaultConstructor->access, defaultConstructor->objectType,
                            "constructor", defaultConstructor->name);
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

DataType TypeChecker::CheckExpression(AstNode* node, std::optional<DataType> expected) {
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
                if (field.name == node->token.lexeme) {
                    result = field.type;
                    node->implicitThis = true;
                    if (!activeLambdas_.empty())
                        Error(node, "anonymous functions cannot capture implicit this; capture an object handle explicitly");
                    CheckAccess(node, field.access, field.objectType, "field", field.name);
                    break;
                }
            }
        }
        if (!result.IsValid() && currentClass_)
            result = CheckImplicitProperty(node);
        if (!result.IsValid()) {
            const auto constant = FindEnumConstant(node->token.lexeme);
            if (constant) result = constant->Type();
        }
        if (!result.IsValid() && node->propertyGetter.empty() && node->propertySetter.empty())
            Error(node, "unknown variable '" + node->token.lexeme + "'");
        break;
    }
    case NodeKind::Member: result = CheckMember(node); break;
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
    case NodeKind::Unary: result = CheckUnary(node, expected); break;
    case NodeKind::Increment: {
        AstNode* operand = node->firstChild;
        result = CheckExpression(operand);
        if (operand && (!operand->propertyGetter.empty() || !operand->propertySetter.empty())) {
            Error(node, "increment and decrement are not supported for property accessors");
            result = DataType::Invalid();
            break;
        }
        if (result.kind == TypeKind::Object) {
            const std::string_view name = node->token.kind == TokenKind::PlusPlus
                ? (node->isPostfix ? "opPostInc" : "opPreInc")
                : (node->isPostfix ? "opPostDec" : "opPreDec");
            const FunctionSignature* method = FindOperatorMethod(result, name, {});
            if (!method) {
                Error(node, "no matching operator overload for '" + node->token.lexeme + "'");
                result = DataType::Invalid();
            } else {
                node->operatorMethod = method->name;
                CheckAccess(node, method->access, method->objectType, "method", method->name);
                result = method->returnType;
            }
            break;
        }
        if (!operand || (operand->kind != NodeKind::Identifier && operand->kind != NodeKind::Member &&
                         !(operand->kind == NodeKind::Call && operand->returnsReference)))
            Error(operand, "increment operand is not assignable");
        if (IsReadOnlyLValue(operand)) Error(operand, "cannot modify const variable");
        if (!result.IsNumeric()) Error(node, "increment operator requires a numeric operand");
        break;
    }
    case NodeKind::Cast: {
        const DataType source = CheckExpression(node->firstChild);
        result = node->declaredType;
        if (result.kind != TypeKind::Object) {
            Error(node, "reference cast target must be a class or interface type");
        } else if (!FindClass(result.objectName)) {
            Error(node, "unknown reference cast target '" + result.objectName + "'");
        }
        if (source.kind != TypeKind::Object || !source.isHandle)
            Error(node, "reference cast source must be an object handle");
        else if (source.objectName != "<null>") {
            const ClassSignature* sourceType = FindClass(source.objectName);
            if (!sourceType || sourceType->host)
                Error(node, "reference cast source must be a script object handle");
        }
        if (source.objectName != "<null>" && result.kind == TypeKind::Object) {
            const FunctionSignature* method = FindOperatorMethod(source, "opCast", {}, result);
            if (!method) method = FindOperatorMethod(source, "opImplCast", {}, result);
            if (method) {
                node->operatorMethod = method->name;
                CheckAccess(node, method->access, method->objectType, "method", method->name);
            }
        }
        break;
    }
    case NodeKind::ValueCast: {
        const DataType source = CheckExpression(node->firstChild);
        result = node->declaredType;
        if (source.kind == TypeKind::Object) {
            const FunctionSignature* method = FindOperatorMethod(source, "opConv", {}, result);
            if (!method) method = FindOperatorMethod(source, "opImplConv", {}, result);
            if (!method) {
                Error(node, "no matching conversion operator to '" + result.Name() + "'");
                result = DataType::Invalid();
            } else {
                node->operatorMethod = method->name;
                CheckAccess(node, method->access, method->objectType, "method", method->name);
            }
        } else if (!(source.IsNumeric() && result.IsNumeric()) &&
                   !(result == DataType::String()) && source != result) {
            Error(node, "cannot explicitly convert " + source.Name() + " to " + result.Name());
            result = DataType::Invalid();
        }
        break;
    }
    case NodeKind::Call: result = CheckCall(node); break;
    case NodeKind::AnonymousFunction: result = CheckAnonymousFunction(node, expected); break;
    case NodeKind::Assign: {
        const auto children = node->Children();
        DataType target;
        if (children[0]->kind == NodeKind::Member) {
            target = CheckMember(children[0], true, node->token.kind != TokenKind::Equal);
        } else if (children[0]->kind == NodeKind::Identifier && currentClass_ &&
                   !Lookup(children[0]->token.lexeme)) {
            target = CheckImplicitProperty(children[0], true, node->token.kind != TokenKind::Equal);
            if (children[0]->propertyGetter.empty() && children[0]->propertySetter.empty())
                target = CheckExpression(children[0]);
        } else {
            target = CheckExpression(children[0]);
        }
        children[0]->inferredType = target;
        const bool explicitHandleTarget = children[0]->kind == NodeKind::Unary &&
            children[0]->token.kind == TokenKind::At && children[0]->firstChild;
        if (target.kind == TypeKind::Function && !explicitHandleTarget)
            Error(children[0], "function handle assignment requires explicit '@' on the target");
        if (children[0]->kind != NodeKind::Identifier && children[0]->kind != NodeKind::Member &&
            !explicitHandleTarget &&
            !(children[0]->kind == NodeKind::Call && children[0]->returnsReference)) {
            Error(children[0], "left side of assignment is not assignable");
        }
        if (children[0]->kind == NodeKind::Identifier && FindEnumConstant(children[0]->token.lexeme)) {
            Error(children[0], "cannot assign to enum value '" + children[0]->token.lexeme + "'");
        } else if (IsReadOnlyLValue(children[0])) {
            Error(children[0], "cannot assign to const variable '" + children[0]->token.lexeme + "'");
        }
        DataType value = CheckExpression(children[1], target);
        const bool weakHandleAssignment = explicitHandleTarget && IsWeakRef(target) &&
            value.kind == TypeKind::Object && value.isHandle &&
            (value.objectName == "<null>" ||
             CanConvert(value, DataType::Object(target.objectName, true)));
        if (!children[0]->propertySetter.empty() && node->token.kind != TokenKind::Equal &&
            !target.IsNumeric() && target != DataType::String()) {
            Error(node, "compound property assignment currently requires a numeric or string property");
            result = DataType::Invalid();
            break;
        }
        const std::string_view operatorName = AssignmentOperatorMethod(node->token.kind);
        if (target.kind == TypeKind::Object && !operatorName.empty()) {
            const FunctionSignature* method = FindOperatorMethod(target, operatorName, {value});
            if (method) {
                node->operatorMethod = method->name;
                CheckAccess(node, method->access, method->objectType, "method", method->name);
                result = method->returnType;
                break;
            }
            const ClassSignature* targetClass = FindClass(target.objectName);
            if (node->token.kind == TokenKind::Equal && !explicitHandleTarget && targetClass &&
                targetClass->defaultCopyAssignmentDeleted) {
                Error(node, "copy assignment for '" + targetClass->name + "' is deleted");
                result = DataType::Invalid();
                break;
            }
            if (node->token.kind != TokenKind::Equal) {
                Error(node, "no matching operator overload for '" + node->token.lexeme + "'");
                result = DataType::Invalid();
                break;
            }
        }
        if (node->token.kind == TokenKind::Equal) {
            if (!weakHandleAssignment && !CanConvert(value, target))
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

DataType TypeChecker::CheckMember(AstNode* node, bool writing, bool compound) {
    if (!node || !node->firstChild) return DataType::Invalid();
    const DataType object = CheckExpression(node->firstChild);
    const ClassSignature* type = FindClass(object.objectName);
    if (!type) {
        Error(node, "unknown object type '" + object.objectName + "'");
        return DataType::Invalid();
    }
    for (const auto& field : type->fields) {
        if (field.name != node->token.lexeme) continue;
        CheckAccess(node, field.access, field.objectType, "field", field.name);
        if (writing && field.isConst)
            Error(node, "field '" + field.name + "' is read-only");
        return field.type;
    }

    const std::string getterName = "get_" + node->token.lexeme;
    const std::string setterName = "set_" + node->token.lexeme;
    const FunctionSignature* getter = nullptr;
    const FunctionSignature* setter = nullptr;
    for (const ClassSignature* owner = type; owner;
         owner = owner->baseClass.empty() ? nullptr : FindClass(owner->baseClass)) {
        for (const auto& method : owner->methods) {
            if (!method.propertyAccessor) continue;
            if (!getter && method.name == getterName && method.parameters.empty() &&
                method.returnType != DataType::Void()) getter = &method;
            if (!setter && method.name == setterName && method.parameters.size() == 1 &&
                method.returnType == DataType::Void()) setter = &method;
        }
    }
    if (!getter && !setter) {
        Error(node, "type '" + type->name + "' has no field or property '" +
                    node->token.lexeme + "'");
        return DataType::Invalid();
    }
    if (getter && setter && getter->returnType != setter->parameters[0]) {
        Error(node, "property getter and setter types must match for '" + node->token.lexeme + "'");
        return DataType::Invalid();
    }
    node->propertyGetter = getter ? getter->name : std::string{};
    node->propertySetter = setter ? setter->name : std::string{};
    if ((compound || !writing) && !getter) {
        Error(node, "property '" + node->token.lexeme + "' is write-only");
        return DataType::Invalid();
    }
    if (writing && !setter) {
        Error(node, "property '" + node->token.lexeme + "' is read-only");
        return DataType::Invalid();
    }
    if (getter && (compound || !writing))
        CheckAccess(node, getter->access, getter->objectType, "property getter", node->token.lexeme);
    if (setter && writing)
        CheckAccess(node, setter->access, setter->objectType, "property setter", node->token.lexeme);
    return getter ? getter->returnType : setter->parameters[0];
}

DataType TypeChecker::CheckImplicitProperty(AstNode* node, bool writing, bool compound) {
    if (!node || !currentClass_) return DataType::Invalid();
    for (const auto& field : currentClass_->fields)
        if (field.name == node->token.lexeme) return DataType::Invalid();
    const std::string getterName = "get_" + node->token.lexeme;
    const std::string setterName = "set_" + node->token.lexeme;
    const FunctionSignature* getter = nullptr;
    const FunctionSignature* setter = nullptr;
    for (const ClassSignature* owner = currentClass_; owner;
         owner = owner->baseClass.empty() ? nullptr : FindClass(owner->baseClass)) {
        for (const auto& method : owner->methods) {
            if (!method.propertyAccessor) continue;
            if (!getter && method.name == getterName && method.parameters.empty() &&
                method.returnType != DataType::Void()) getter = &method;
            if (!setter && method.name == setterName && method.parameters.size() == 1 &&
                method.returnType == DataType::Void()) setter = &method;
        }
    }
    if (!getter && !setter) return DataType::Invalid();
    node->propertyGetter = getter ? getter->name : std::string{};
    node->propertySetter = setter ? setter->name : std::string{};
    node->implicitThis = true;
    if (getter && setter && getter->returnType != setter->parameters[0]) {
        Error(node, "property getter and setter types must match for '" + node->token.lexeme + "'");
        return DataType::Invalid();
    }
    if ((compound || !writing) && !getter) {
        Error(node, "property '" + node->token.lexeme + "' is write-only");
        return DataType::Invalid();
    }
    if (writing && !setter) {
        Error(node, "property '" + node->token.lexeme + "' is read-only");
        return DataType::Invalid();
    }
    if (getter && (compound || !writing))
        CheckAccess(node, getter->access, getter->objectType, "property getter", node->token.lexeme);
    if (setter && writing)
        CheckAccess(node, setter->access, setter->objectType, "property setter", node->token.lexeme);
    return getter ? getter->returnType : setter->parameters[0];
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
    const auto selectOperator = [&](std::string_view directName, std::string_view reverseName,
                                    std::optional<DataType> requiredReturn)
        -> const FunctionSignature* {
        const FunctionSignature* direct = directName.empty() ? nullptr
            : FindOperatorMethod(left, directName, {right}, requiredReturn);
        const FunctionSignature* reverse = reverseName.empty() ? nullptr
            : FindOperatorMethod(right, reverseName, {left}, requiredReturn);
        const auto directCost = direct ? MatchArguments(*direct, {right}, {std::string{}}) : std::nullopt;
        const auto reverseCost = reverse ? MatchArguments(*reverse, {left}, {std::string{}}) : std::nullopt;
        const bool useReverse = reverse && (!direct || (reverseCost && directCost && *reverseCost < *directCost));
        const FunctionSignature* selected = useReverse ? reverse : direct;
        if (!selected) return nullptr;
        node->operatorMethod = selected->name;
        node->operatorReversed = useReverse;
        CheckAccess(node, selected->access, selected->objectType, "method", selected->name);
        return selected;
    };
    const bool equality = op == TokenKind::EqualEqual || op == TokenKind::BangEqual ||
                          op == TokenKind::KwIs;
    if (equality && (IsWeakRef(left) || IsWeakRef(right))) {
        bool comparable = false;
        if (IsWeakRef(left) && IsWeakRef(right))
            comparable = left.objectName == right.objectName;
        else {
            const DataType& weak = IsWeakRef(left) ? left : right;
            const DataType& object = IsWeakRef(left) ? right : left;
            comparable = object.kind == TypeKind::Object && object.isHandle &&
                (object.objectName == "<null>" ||
                 CanConvert(object, DataType::Object(weak.objectName, true)));
        }
        if (!comparable) Error(node, "incomparable weakref operand types");
        return DataType::Bool();
    }
    if (op != TokenKind::KwIs && equality &&
        (left.kind == TypeKind::Object || right.kind == TypeKind::Object)) {
        if (selectOperator("opEquals", "opEquals", DataType::Bool()))
            return DataType::Bool();
        if (selectOperator("opCmp", "opCmp", DataType::Int()))
            return DataType::Bool();
        Error(node, "no matching operator overload for '" + node->token.lexeme + "'");
        return DataType::Invalid();
    }
    const bool ordering = op == TokenKind::Less || op == TokenKind::LessEqual ||
                          op == TokenKind::Greater || op == TokenKind::GreaterEqual;
    if (ordering && (left.kind == TypeKind::Object || right.kind == TypeKind::Object)) {
        if (selectOperator("opCmp", "opCmp", DataType::Int()))
            return DataType::Bool();
    }
    const auto operatorNames = BinaryOperatorMethods(op);
    if ((!operatorNames.first.empty()) &&
        (left.kind == TypeKind::Object || right.kind == TypeKind::Object)) {
        if (const FunctionSignature* method = selectOperator(
                operatorNames.first, operatorNames.second, std::nullopt)) return method->returnType;
        Error(node, "no matching operator overload for '" + node->token.lexeme + "'");
        return DataType::Invalid();
    }
    if (op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret ||
        op == TokenKind::ShiftLeft || op == TokenKind::ShiftRight ||
        op == TokenKind::ShiftRightArithmetic) {
        if (!left.IsInteger() || !right.IsInteger())
            Error(node, "bitwise operator requires integer operands");
        return left.IsInteger() ? left : DataType::Invalid();
    }
    if (equality) {
        const bool relatedObjects = left.kind == TypeKind::Object && right.kind == TypeKind::Object &&
            (CanConvert(left, right) || CanConvert(right, left));
        const bool relatedFunctions =
            (left.kind == TypeKind::Function && right.kind == TypeKind::Function && left == right) ||
            (left.kind == TypeKind::Function && right.kind == TypeKind::Object &&
             right.objectName == "<null>") ||
            (right.kind == TypeKind::Function && left.kind == TypeKind::Object &&
             left.objectName == "<null>");
        if (left != right && !(left.IsNumeric() && right.IsNumeric()) &&
            !relatedObjects && !relatedFunctions)
            Error(node, "incomparable operand types");
        return DataType::Bool();
    }
    if (!left.IsNumeric() || !right.IsNumeric()) {
        Error(node, "operator requires numeric operands"); return DataType::Invalid();
    }
    if (ordering) return DataType::Bool();
    return CommonNumericType(left, right);
}

DataType TypeChecker::CheckUnary(AstNode* node, std::optional<DataType> expected) {
    if (node->token.kind == TokenKind::At && node->firstChild &&
        node->firstChild->kind == NodeKind::Identifier &&
        !Lookup(node->firstChild->token.lexeme)) {
        if (ResolveFunctionAddress(node, expected)) return node->declaredType;
        return DataType::Invalid();
    }
    DataType operand = CheckExpression(node->firstChild);
    if (operand.kind == TypeKind::Object &&
        (node->token.kind == TokenKind::Minus || node->token.kind == TokenKind::Tilde)) {
        const std::string_view name = node->token.kind == TokenKind::Minus ? "opNeg" : "opCom";
        const FunctionSignature* method = FindOperatorMethod(operand, name, {});
        if (!method) {
            Error(node, "no matching operator overload for '" + node->token.lexeme + "'");
            return DataType::Invalid();
        }
        node->operatorMethod = method->name;
        CheckAccess(node, method->access, method->objectType, "method", method->name);
        return method->returnType;
    }
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
    if (callee->kind == NodeKind::Identifier && IsWeakRef(callee->declaredType)) {
        const DataType weakType = callee->declaredType;
        if (!FindClass(weakType.objectName))
            Error(callee, "weakref subtype must name a script class");
        AstNode* argument = callee->nextSibling;
        if (argument && argument->nextSibling)
            Error(node, "weakref construction accepts zero or one object handle");
        if (argument) {
            const DataType value = CheckExpression(argument,
                DataType::Object(weakType.objectName, true));
            if (value.kind != TypeKind::Object || !value.isHandle ||
                (value.objectName != "<null>" &&
                 !CanConvert(value, DataType::Object(weakType.objectName, true)))) {
                Error(argument, "weakref constructor requires a compatible object handle");
            }
        }
        node->operatorMethod = "$weakref.construct";
        callee->inferredType = weakType;
        return weakType;
    }
    if (callee->kind == NodeKind::Member && callee->token.lexeme == "get" &&
        callee->firstChild) {
        const DataType weakType = CheckExpression(callee->firstChild);
        if (IsWeakRef(weakType)) {
            if (callee->nextSibling)
                Error(node, "weakref.get() does not accept arguments");
            node->operatorMethod = "$weakref.get";
            callee->inferredType = weakType;
            return DataType::Object(weakType.objectName, true);
        }
    }
    if (callee->kind == NodeKind::Identifier) {
        if (const FuncdefSignature* delegateType = FindFuncdef(callee->token.lexeme)) {
            AstNode* argument = callee->nextSibling;
            if (!argument || argument->nextSibling || argument->kind != NodeKind::Member) {
                Error(node, "delegate construction requires exactly one object method");
                return DataType::Invalid();
            }
            const DataType receiver = CheckExpression(argument->firstChild);
            const ClassSignature* owner = FindClass(receiver.objectName);
            const FunctionSignature* method = owner
                ? FindExactMethod(owner, argument->token.lexeme,
                                  delegateType->signature.parameters,
                                  delegateType->signature.parameterModes)
                : nullptr;
            if (!method || !SameCallableSignature(*method, delegateType->signature)) {
                Error(argument, "no method matching funcdef '" + delegateType->name + "'");
                return DataType::Invalid();
            }
            if (method->host) {
                Error(argument, "delegates to registered object methods are not supported yet");
                return DataType::Invalid();
            }
            CheckAccess(argument, method->access, method->objectType, "method", method->name);
            node->operatorMethod = method->Declaration();
            node->delegateObjectType = method->objectType;
            node->declaredType = DataType::Function(delegateType->name, true);
            callee->inferredType = node->declaredType;
            argument->inferredType = node->declaredType;
            return node->declaredType;
        }
    }
    const FuncdefSignature* handleType = nullptr;
    if (callee->kind == NodeKind::Identifier) {
        const auto symbol = Lookup(callee->token.lexeme);
        if (symbol && symbol->type.kind == TypeKind::Function) {
            handleType = FindFuncdef(symbol->type.objectName);
            callee->inferredType = symbol->type;
        } else if (!symbol && currentClass_) {
            for (const auto& field : currentClass_->fields) {
                if (field.name == callee->token.lexeme && field.type.kind == TypeKind::Function) {
                    handleType = FindFuncdef(field.type.objectName);
                    callee->inferredType = field.type;
                    callee->implicitThis = true;
                    CheckAccess(callee, field.access, field.objectType, "field", field.name);
                    break;
                }
            }
        }
    } else if (callee->kind == NodeKind::Member && callee->firstChild) {
        const DataType receiver = CheckExpression(callee->firstChild);
        if (const ClassSignature* owner = FindClass(receiver.objectName)) {
            for (const auto& field : owner->fields) {
                if (field.name == callee->token.lexeme && field.type.kind == TypeKind::Function) {
                    handleType = FindFuncdef(field.type.objectName);
                    callee->inferredType = field.type;
                    CheckAccess(callee, field.access, field.objectType, "field", field.name);
                    break;
                }
            }
        }
    } else {
        const DataType callable = CheckExpression(callee);
        if (callable.kind == TypeKind::Function) handleType = FindFuncdef(callable.objectName);
    }
    std::vector<DataType> arguments;
    std::vector<AstNode*> argumentNodes;
    std::vector<std::string> argumentNames;
    for (AstNode* argument = callee->nextSibling; argument; argument = argument->nextSibling) {
        AstNode* expression = argument->kind == NodeKind::NamedArgument ? argument->firstChild : argument;
        const std::size_t index = arguments.size();
        const DataType type = CheckExpression(
            expression, handleType && index < handleType->signature.parameters.size()
                ? std::optional<DataType>{handleType->signature.parameters[index]}
                : std::nullopt);
        argument->inferredType = type;
        arguments.push_back(type);
        argumentNodes.push_back(expression);
        argumentNames.push_back(argument->kind == NodeKind::NamedArgument ? argument->token.lexeme
                                                                          : std::string{});
    }
    if (handleType) {
        const auto cost = MatchArguments(handleType->signature, arguments, argumentNames);
        if (!cost) {
            Error(node, "arguments do not match funcdef '" + handleType->name + "'");
            return DataType::Invalid();
        }
        ValidateReferenceArguments(handleType->signature, argumentNodes, argumentNames);
        node->returnsReference = handleType->signature.returnsReference;
        node->returnReferenceConst = handleType->signature.returnReferenceConst;
        return handleType->signature.returnType;
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
        if (base && base->defaultConstructorDeleted && arguments.empty() && !constructor)
            Error(node, "default constructor for base class '" + currentClass_->baseClass + "' is deleted");
        else if ((hasConstructors && !constructor) || (!hasConstructors && !arguments.empty()))
            Error(node, "no matching base constructor for '" + currentClass_->baseClass + "'");
        else if (constructor) {
            ValidateReferenceArguments(*constructor, argumentNodes, argumentNames);
            CheckAccess(node, constructor->access, constructor->objectType,
                        "constructor", constructor->name);
        }
        node->nonVirtualCall = true;
        return DataType::Void();
    }
    if (callee->kind == NodeKind::Identifier) {
        if (const ClassSignature* type = FindClass(callee->token.lexeme)) {
            if (type->interfaceType) {
                Error(node, "interface types cannot be constructed");
                return DataType::Invalid();
            }
            if (type->host) {
                const FunctionSignature* factory = nullptr;
                int bestCost = 1000000;
                for (const auto& candidate : functions_) {
                    if (!candidate.factory || candidate.objectType != type->name) continue;
                    const auto cost = MatchArguments(candidate, arguments, argumentNames);
                    if (cost && *cost < bestCost) { factory = &candidate; bestCost = *cost; }
                }
                if (!factory) {
                    Error(node, "no matching factory for '" + type->name + "'");
                    return DataType::Invalid();
                }
                ValidateReferenceArguments(*factory, argumentNodes, argumentNames);
                return DataType::Object(type->name, true);
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
            if (type->defaultConstructorDeleted && arguments.empty() && !constructor) {
                Error(node, "default constructor for '" + type->name + "' is deleted");
                return DataType::Invalid();
            }
            const bool generatedCopy = type->generatedCopyConstructor && arguments.size() == 1 &&
                argumentNames[0].empty() &&
                ConversionCost(arguments[0], DataType::Object(type->name, true)).has_value();
            if (!constructor && !generatedCopy &&
                ((hasConstructors && !constructor) || (!hasConstructors && !arguments.empty()))) {
                const DataType target = DataType::Object(type->name, true);
                const FunctionSignature* conversion = arguments.size() == 1 && argumentNames[0].empty()
                    ? FindOperatorMethod(arguments[0], "opConv", {}, target) : nullptr;
                if (!conversion && arguments.size() == 1 && argumentNames[0].empty())
                    conversion = FindOperatorMethod(arguments[0], "opImplConv", {}, target);
                if (!conversion) {
                    Error(node, "no matching constructor for '" + type->name + "'");
                    return DataType::Invalid();
                }
                node->operatorMethod = conversion->name;
                CheckAccess(node, conversion->access, conversion->objectType,
                            "method", conversion->name);
                return target;
            }
            if (constructor) {
                ValidateReferenceArguments(*constructor, argumentNodes, argumentNames);
                CheckAccess(node, constructor->access, constructor->objectType,
                            "constructor", constructor->name);
            }
            return DataType::Object(type->name, true);
        }
        const auto callableObject = Lookup(callee->token.lexeme);
        if (callableObject && callableObject->type.kind == TypeKind::Object) {
            callee->inferredType = callableObject->type;
            const FunctionSignature* method = FindMethod(
                callableObject->type, "opCall", arguments, argumentNames);
            if (!method) {
                Error(node, "callee is not callable");
                return DataType::Invalid();
            }
            ValidateReferenceArguments(*method, argumentNodes, argumentNames);
            CheckAccess(node, method->access, method->objectType, "method", method->name);
            node->operatorMethod = method->name;
            node->returnsReference = method->returnsReference;
            node->returnReferenceConst = method->returnReferenceConst;
            return method->returnType;
        }
    }
    if (callee->kind == NodeKind::Member) {
        const DataType object = CheckExpression(callee->firstChild);
        const FunctionSignature* method = FindMethod(object, callee->token.lexeme, arguments, argumentNames);
        if (!method) Error(node, "no matching method for '" + callee->token.lexeme + "'");
        else {
            ValidateReferenceArguments(*method, argumentNodes, argumentNames);
            CheckAccess(node, method->access, method->objectType, "method", method->name);
            node->returnsReference = method->returnsReference;
            node->returnReferenceConst = method->returnReferenceConst;
        }
        return method ? method->returnType : DataType::Invalid();
    }
    if (callee->kind != NodeKind::Identifier) {
        const DataType object = CheckExpression(callee);
        const FunctionSignature* method = object.kind == TypeKind::Object
            ? FindMethod(object, "opCall", arguments, argumentNames) : nullptr;
        if (!method) {
            Error(node, "callee is not callable");
            return DataType::Invalid();
        }
        ValidateReferenceArguments(*method, argumentNodes, argumentNames);
        CheckAccess(node, method->access, method->objectType, "method", method->name);
        node->operatorMethod = method->name;
        node->returnsReference = method->returnsReference;
        node->returnReferenceConst = method->returnReferenceConst;
        return method->returnType;
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
                CheckAccess(node, method->access, method->objectType, "method", method->name);
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
            CheckAccess(node, method->access, method->objectType, "method", method->name);
            node->returnsReference = method->returnsReference;
            node->returnReferenceConst = method->returnReferenceConst;
            return method->returnType;
        }
    }
    const FunctionSignature* best = nullptr;
    int bestCost = 1000000;
    for (const auto& function : functions_) {
        if (function.factory) continue;
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

DataType TypeChecker::CheckAnonymousFunction(AstNode* node,
                                             std::optional<DataType> expected) {
    std::vector<AstNode*> parameters;
    AstNode* body = node ? node->firstChild : nullptr;
    while (body && body->kind == NodeKind::Parameter) {
        parameters.push_back(body);
        body = body->nextSibling;
    }
    if (!body || body->kind != NodeKind::Block) {
        Error(node, "anonymous function body is missing");
        return DataType::Invalid();
    }

    const FuncdefSignature* required = expected && expected->kind == TypeKind::Function
        ? FindFuncdef(expected->objectName) : nullptr;
    if (expected && expected->kind == TypeKind::Function && !required) {
        Error(node, "unknown funcdef type '" + expected->objectName + "'");
        return DataType::Invalid();
    }
    const FuncdefSignature* selected = nullptr;
    bool ambiguous = false;
    for (const auto& funcdef : funcdefs_) {
        if (required && funcdef.name != required->name) continue;
        if (funcdef.signature.parameters.size() != parameters.size()) continue;
        bool matches = true;
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (parameters[index]->declaredType.IsValid() &&
                parameters[index]->declaredType != funcdef.signature.parameters[index]) matches = false;
            if (parameters[index]->declaredType.IsValid() &&
                parameters[index]->parameterMode != funcdef.signature.parameterModes[index]) matches = false;
        }
        if (!matches) continue;
        if (selected) ambiguous = true;
        else selected = &funcdef;
    }
    if (!selected || ambiguous) {
        Error(node, ambiguous ? "anonymous function signature is ambiguous"
                              : "no funcdef matches anonymous function parameters");
        return DataType::Invalid();
    }

    node->token.lexeme = "$lambda$" + node->token.location.section + "$" +
        std::to_string(node->token.location.row) + "$" +
        std::to_string(node->token.location.column);
    node->declaredType = DataType::Function(selected->name, true);
    node->returnsReference = selected->signature.returnsReference;
    node->returnReferenceConst = selected->signature.returnReferenceConst;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        parameters[index]->declaredType = selected->signature.parameters[index];
        parameters[index]->parameterMode = selected->signature.parameterModes[index];
    }

    FunctionSignature signature = selected->signature;
    signature.name = node->token.lexeme;
    signature.parameterNames.clear();
    for (AstNode* parameter : parameters) signature.parameterNames.push_back(parameter->token.lexeme);
    functions_.push_back(std::move(signature));

    const DataType previousReturn = currentReturn_;
    const bool previousReturnsReference = currentReturnsReference_;
    const int previousBreakable = breakableDepth_;
    const int previousLoop = loopDepth_;
    currentReturn_ = selected->signature.returnType;
    currentReturnsReference_ = selected->signature.returnsReference;
    breakableDepth_ = 0;
    loopDepth_ = 0;
    scopes_.emplace_back();
    const std::size_t lambdaScope = scopes_.size() - 1;
    activeLambdas_.push_back({node, lambdaScope});
    for (AstNode* parameter : parameters)
        Declare(parameter->token, parameter->declaredType,
                parameter->parameterMode == ParameterMode::In);
    CheckBlock(body, false);
    activeLambdas_.pop_back();
    scopes_.pop_back();
    currentReturn_ = previousReturn;
    currentReturnsReference_ = previousReturnsReference;
    breakableDepth_ = previousBreakable;
    loopDepth_ = previousLoop;
    return node->declaredType;
}

std::optional<TypeChecker::VariableSymbol> TypeChecker::Lookup(std::string_view name) const {
    for (const auto& candidate : NameCandidates(currentNamespace_, name)) {
        for (std::size_t index = scopes_.size(); index > 0; --index) {
            const auto found = scopes_[index - 1].find(candidate);
            if (found != scopes_[index - 1].end()) {
                if (index - 1 > 0) {
                    for (const auto& lambda : activeLambdas_) {
                        if (index - 1 >= lambda.second) continue;
                        auto& captures = lambda.first->captureNames;
                        if (std::find(captures.begin(), captures.end(), found->first) == captures.end())
                            captures.push_back(found->first);
                    }
                }
                return found->second;
            }
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

const FunctionSignature* TypeChecker::FindOperatorMethod(
    const DataType& object, std::string_view name, const std::vector<DataType>& arguments,
    std::optional<DataType> requiredReturn) const {
    if (object.kind != TypeKind::Object || !object.isHandle) return nullptr;
    const std::vector<std::string> unnamed(arguments.size());
    const ClassSignature* type = FindClass(object.objectName);
    while (type) {
        const FunctionSignature* best = nullptr;
        int bestCost = 1000000;
        for (const auto& method : type->methods) {
            if (method.constructor || method.destructor || method.name != name ||
                (requiredReturn && method.returnType != *requiredReturn)) continue;
            const auto cost = MatchArguments(method, arguments, unnamed);
            if (cost && *cost < bestCost) {
                best = &method;
                bestCost = *cost;
            }
        }
        if (best) return best;
        type = type->baseClass.empty() ? nullptr : FindClass(type->baseClass);
    }
    return nullptr;
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
        if (argument->kind == NodeKind::Member &&
            (!argument->propertyGetter.empty() || !argument->propertySetter.empty())) {
            Error(argument, "property accessors cannot be passed to out or inout parameters");
            valid = false;
        } else if (argument->kind != NodeKind::Identifier && argument->kind != NodeKind::Member) {
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
    if (node->kind == NodeKind::Unary && node->token.kind == TokenKind::At)
        return IsReadOnlyLValue(node->firstChild);
    if (node->kind == NodeKind::Identifier) {
        const auto symbol = Lookup(node->token.lexeme);
        return symbol && symbol->isConst;
    }
    if (node->kind == NodeKind::Member) {
        const DataType receiver = node->firstChild ? node->firstChild->inferredType
                                                   : DataType::Invalid();
        if (const ClassSignature* type = FindClass(receiver.objectName)) {
            for (const auto& field : type->fields)
                if (field.name == node->token.lexeme && field.isConst) return true;
        }
        return IsReadOnlyLValue(node->firstChild);
    }
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

const FuncdefSignature* TypeChecker::FindFuncdef(std::string_view name) const {
    if (name.find("::") == std::string_view::npos && currentClass_) {
        for (const ClassSignature* type = currentClass_; type;
             type = type->baseClass.empty() ? nullptr : FindClass(type->baseClass)) {
            const std::string child = type->name + "::" + std::string(name);
            for (const auto& funcdef : funcdefs_) if (funcdef.name == child) return &funcdef;
        }
    }
    for (const auto& candidate : NameCandidates(currentNamespace_, name))
        for (const auto& type : funcdefs_) if (type.name == candidate) return &type;
    return nullptr;
}

const FunctionSignature* TypeChecker::ResolveFunctionAddress(
    AstNode* node, std::optional<DataType> expected, bool reportErrors) {
    if (!node || !node->firstChild || node->firstChild->kind != NodeKind::Identifier)
        return nullptr;
    const FuncdefSignature* required = expected && expected->kind == TypeKind::Function
        ? FindFuncdef(expected->objectName) : nullptr;
    if (expected && expected->kind == TypeKind::Function && !required) {
        if (reportErrors) Error(node, "unknown funcdef type '" + expected->objectName + "'");
        return nullptr;
    }

    const FunctionSignature* selected = nullptr;
    const FuncdefSignature* selectedType = nullptr;
    int bestCost = 1000000;
    bool ambiguous = false;
    for (const auto& function : functions_) {
        if (function.method || function.constructor || function.destructor || function.factory) continue;
        const auto nameCost = NameMatchCost(function.name, node->firstChild->token.lexeme,
                                            currentNamespace_);
        if (!nameCost) continue;
        for (const auto& funcdef : funcdefs_) {
            if (required && funcdef.name != required->name) continue;
            if (!SameCallableSignature(function, funcdef.signature)) continue;
            if (*nameCost < bestCost) {
                selected = &function;
                selectedType = &funcdef;
                bestCost = *nameCost;
                ambiguous = false;
            } else if (*nameCost == bestCost &&
                       (selected != &function || selectedType != &funcdef)) {
                ambiguous = true;
            }
        }
    }
    if (!selected || !selectedType || ambiguous) {
        if (reportErrors) {
            Error(node, ambiguous
                ? "function address is ambiguous for '" + node->firstChild->token.lexeme + "'"
                : "no function matching a funcdef for '" + node->firstChild->token.lexeme + "'");
        }
        return nullptr;
    }
    node->operatorMethod = selected->Declaration();
    node->declaredType = DataType::Function(selectedType->name, true);
    node->firstChild->inferredType = node->declaredType;
    return selected;
}

bool TypeChecker::IsDerivedFrom(std::string_view derived, std::string_view base) const {
    const ClassSignature* type = FindClass(derived);
    while (type) {
        if (type->name == base) return true;
        type = type->baseClass.empty() ? nullptr : FindClass(type->baseClass);
    }
    return false;
}

bool TypeChecker::CanAccess(MemberAccess access, std::string_view declaringType) const {
    if (access == MemberAccess::Public) return true;
    if (!currentClass_) return false;
    if (currentClass_->name == declaringType) return true;
    return access == MemberAccess::Protected &&
           IsDerivedFrom(currentClass_->name, declaringType);
}

void TypeChecker::CheckAccess(const AstNode* node, MemberAccess access,
                              std::string_view declaringType, std::string_view memberKind,
                              std::string_view memberName) {
    if (CanAccess(access, declaringType)) return;
    const char* visibility = access == MemberAccess::Private ? "private" : "protected";
    Error(node, "cannot access " + std::string(visibility) + " " + std::string(memberKind) +
                " '" + std::string(declaringType) + "::" + std::string(memberName) + "'");
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
    if (from.kind == TypeKind::WeakRef && to.kind == TypeKind::ConstWeakRef &&
        from.objectName == to.objectName) return true;
    if (IsWeakRef(from) && to.kind == TypeKind::Object && to.isHandle &&
        (from.objectName == to.objectName || IsDerivedFrom(from.objectName, to.objectName)))
        return true;
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
    if (from.kind == TypeKind::Object && from.isHandle) {
        const FunctionSignature* method = FindOperatorMethod(from, "opImplConv", {}, to);
        if (!method) method = FindOperatorMethod(from, "opImplCast", {}, to);
        if (method && CanAccess(method->access, method->objectType)) return true;
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
