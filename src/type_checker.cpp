#include "mini_as/type_checker.hpp"
#include "mini_as/constant_evaluator.hpp"

#include <limits>
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
    out << returnType.Name() << ' ' << name << '(';
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i) out << ", ";
        out << parameters[i].Name();
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
        if (node->kind == NodeKind::ClassDecl && child && child->kind == NodeKind::Identifier) {
            type.interfaces.push_back(child->token.lexeme);
            child = child->nextSibling;
        }
        for (; child; child = child->nextSibling) {
            if (child->kind == NodeKind::FieldDecl) type.fields.push_back({child->token.lexeme, child->declaredType});
            else if (child->kind == NodeKind::FunctionDecl) {
                FunctionSignature method{child->token.lexeme, child->declaredType, {}, false, {},
                                         type.name, true, child->isConstructor};
                for (AstNode* parameter = child->firstChild;
                     parameter && parameter->kind == NodeKind::Parameter; parameter = parameter->nextSibling)
                    method.parameters.push_back(parameter->declaredType);
                bool duplicate = false;
                for (const auto& existing : type.methods) {
                    if (existing.name == method.name && existing.parameters == method.parameters &&
                        existing.constructor == method.constructor) duplicate = true;
                }
                if (duplicate) Error(child, "duplicate method or constructor '" + method.Declaration() + "'");
                type.methods.push_back(std::move(method));
            }
        }
        classes_.push_back(std::move(type));
    }
    for (const auto& type : classes_) {
        if (type.interfaceType) continue;
        for (const auto& interfaceName : type.interfaces) {
            const ClassSignature* interfaceType = FindClass(interfaceName);
            if (!interfaceType || !interfaceType->interfaceType) continue;
            for (const auto& required : interfaceType->methods) {
                bool found = false;
                for (const auto& method : type.methods) {
                    if (method.name == required.name && method.returnType == required.returnType &&
                        method.parameters == required.parameters) found = true;
                }
                if (!found) Error(root, "class '" + type.name + "' does not implement " +
                                        interfaceName + "::" + required.Declaration());
            }
        }
    }
    for (AstNode* node : TopLevelDeclarations(root)) {
        if (node->kind != NodeKind::FunctionDecl) continue;
        FunctionSignature signature{node->token.lexeme, node->declaredType, {}, false, {}, {}, false, false};
        for (AstNode* child = node->firstChild; child && child->kind == NodeKind::Parameter;
             child = child->nextSibling) signature.parameters.push_back(child->declaredType);
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
            Declare(declaration->token, declaration->declaredType, declaration->isConst);
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
            Declare(declaration->token, declaration->declaredType, declaration->isConst);
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
        if (!CanConvert(value, currentReturn_)) {
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
        for (AstNode* member = node->firstChild; member; member = member->nextSibling) {
            if (member->kind == NodeKind::FieldDecl && member->firstChild) {
                const DataType value = CheckExpression(member->firstChild);
                if (!CanConvert(value, member->declaredType)) {
                    Error(member, "cannot initialize field " + member->declaredType.Name() +
                                  " with " + value.Name());
                }
            }
            if (member->kind == NodeKind::FunctionDecl && member->firstChild) CheckFunction(member);
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
    const std::string previousNamespace = currentNamespace_;
    currentNamespace_ = currentClass_ ? NamespaceOf(currentClass_->name)
                                      : NamespaceOf(node->token.lexeme);
    currentReturn_ = node->declaredType;
    scopes_.emplace_back();
    AstNode* child = node->firstChild;
    while (child && child->kind == NodeKind::Parameter) {
        Declare(child->token, child->declaredType);
        child = child->nextSibling;
    }
    if (child && child->kind == NodeKind::Block) CheckBlock(child, false);
    scopes_.pop_back();
    currentReturn_ = previousReturn;
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
        if (!operand || (operand->kind != NodeKind::Identifier && operand->kind != NodeKind::Member))
            Error(operand, "increment operand is not assignable");
        if (IsReadOnlyLValue(operand)) Error(operand, "cannot modify const variable");
        result = CheckExpression(operand);
        if (!result.IsNumeric()) Error(node, "increment operator requires a numeric operand");
        break;
    }
    case NodeKind::Call: result = CheckCall(node); break;
    case NodeKind::Assign: {
        const auto children = node->Children();
        if (children[0]->kind != NodeKind::Identifier && children[0]->kind != NodeKind::Member) {
            Error(children[0], "left side of assignment is not assignable");
        }
        if (children[0]->kind == NodeKind::Identifier && FindEnumConstant(children[0]->token.lexeme)) {
            Error(children[0], "cannot assign to enum value '" + children[0]->token.lexeme + "'");
        } else if (IsReadOnlyLValue(children[0])) {
            Error(children[0], "cannot assign to const variable '" + children[0]->token.lexeme + "'");
        }
        DataType target = CheckExpression(children[0]);
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
        if (left != right && !(left.IsNumeric() && right.IsNumeric())) Error(node, "incomparable operand types");
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
    for (AstNode* argument = callee->nextSibling; argument; argument = argument->nextSibling) {
        arguments.push_back(CheckExpression(argument));
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
                if (candidate.parameters.size() != arguments.size()) continue;
                int cost = 0;
                bool viable = true;
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    const auto conversion = ConversionCost(arguments[i], candidate.parameters[i]);
                    if (!conversion) { viable = false; break; }
                    cost += *conversion;
                }
                if (viable && cost < bestCost) { constructor = &candidate; bestCost = cost; }
            }
            if ((hasConstructors && !constructor) || (!hasConstructors && !arguments.empty())) {
                Error(node, "no matching constructor for '" + type->name + "'");
                return DataType::Invalid();
            }
            return DataType::Object(type->name, true);
        }
    }
    if (callee->kind == NodeKind::Member) {
        const DataType object = CheckExpression(callee->firstChild);
        const FunctionSignature* method = FindMethod(object, callee->token.lexeme, arguments);
        if (!method) Error(node, "no matching method for '" + callee->token.lexeme + "'");
        return method ? method->returnType : DataType::Invalid();
    }
    if (callee->kind != NodeKind::Identifier) {
        Error(node, "callee is not callable"); return DataType::Invalid();
    }
    if (currentClass_) {
        const FunctionSignature* method = FindMethod(
            DataType::Object(currentClass_->name, true), callee->token.lexeme, arguments);
        if (method) return method->returnType;
    }
    const FunctionSignature* best = nullptr;
    int bestCost = 1000000;
    for (const auto& function : functions_) {
        const auto nameCost = NameMatchCost(function.name, callee->token.lexeme, currentNamespace_);
        if (!nameCost || function.parameters.size() != arguments.size()) continue;
        int cost = *nameCost;
        bool viable = true;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto conversion = ConversionCost(arguments[i], function.parameters[i]);
            if (!conversion) { viable = false; break; }
            cost += *conversion;
        }
        if (viable && cost < bestCost) { best = &function; bestCost = cost; }
    }
    if (!best) {
        Error(node, "no matching function for '" + callee->token.lexeme + "'");
        return DataType::Invalid();
    }
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
    const DataType& object, std::string_view name, const std::vector<DataType>& arguments) const {
    const ClassSignature* type = FindClass(object.objectName);
    if (!type) return nullptr;
    const FunctionSignature* best = nullptr;
    int bestCost = 1000000;
    for (const auto& method : type->methods) {
        if (method.constructor || method.name != name || method.parameters.size() != arguments.size()) continue;
        int cost = 0;
        bool viable = true;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto conversion = ConversionCost(arguments[i], method.parameters[i]);
            if (!conversion) { viable = false; break; }
            cost += *conversion;
        }
        if (viable && cost < bestCost) { best = &method; bestCost = cost; }
    }
    return best;
}

bool TypeChecker::IsReadOnlyLValue(const AstNode* node) const {
    if (!node) return false;
    if (node->kind == NodeKind::Identifier) {
        const auto symbol = Lookup(node->token.lexeme);
        return symbol && symbol->isConst;
    }
    if (node->kind == NodeKind::Member) return IsReadOnlyLValue(node->firstChild);
    return false;
}

const ClassSignature* TypeChecker::FindClass(std::string_view name) const {
    for (const auto& candidate : NameCandidates(currentNamespace_, name))
        for (const auto& type : classes_) if (type.name == candidate) return &type;
    return nullptr;
}

void TypeChecker::Declare(const Token& name, const DataType& type, bool isConst) {
    auto& scope = scopes_.back();
    if (scope.find(name.lexeme) != scope.end()) {
        diagnostics_.Report(name.location, Severity::Error, "duplicate variable '" + name.lexeme + "'");
    } else scope.emplace(name.lexeme, VariableSymbol{type, isConst});
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
