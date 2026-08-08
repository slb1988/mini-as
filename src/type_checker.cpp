#include "mini_as/type_checker.hpp"
#include "mini_as/constant_evaluator.hpp"

#include <sstream>
#include <unordered_set>
#include <utility>

namespace mini_as {

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
    Predeclare(root);
    PredeclareGlobals(root);
    if (root) for (AstNode* child = root->firstChild; child; child = child->nextSibling) CheckNode(child);
    return !diagnostics_.HasErrors();
}

const std::vector<FunctionSignature>& TypeChecker::Functions() const { return functions_; }
const std::vector<ClassSignature>& TypeChecker::Classes() const { return classes_; }
const std::vector<GlobalSignature>& TypeChecker::Globals() const { return globals_; }

void TypeChecker::Predeclare(AstNode* root) {
    if (!root) return;
    classes_.clear();
    for (AstNode* node = root->firstChild; node; node = node->nextSibling) {
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
                FunctionSignature method{child->token.lexeme, child->declaredType, {}, false, {}};
                for (AstNode* parameter = child->firstChild;
                     parameter && parameter->kind == NodeKind::Parameter; parameter = parameter->nextSibling)
                    method.parameters.push_back(parameter->declaredType);
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
    for (AstNode* node = root->firstChild; node; node = node->nextSibling) {
        if (node->kind != NodeKind::FunctionDecl) continue;
        FunctionSignature signature{node->token.lexeme, node->declaredType, {}, false, {}};
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
    for (AstNode* node = root->firstChild; node; node = node->nextSibling) {
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
    for (AstNode* node = root->firstChild; node; node = node->nextSibling) {
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
            if (!sharedAutoType.IsValid()) sharedAutoType = CheckExpression(declaration->firstChild);
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
        break;
    }
    case NodeKind::IfStmt:
    case NodeKind::WhileStmt: {
        AstNode* condition = node->firstChild;
        if (CheckExpression(condition) != DataType::Bool()) Error(condition, "condition must be bool");
        for (AstNode* branch = condition ? condition->nextSibling : nullptr; branch; branch = branch->nextSibling) {
            CheckNode(branch);
        }
        break;
    }
    case NodeKind::ForStmt: {
        const auto children = node->Children();
        scopes_.emplace_back();
        if (children[0]->kind != NodeKind::EmptyStmt) CheckNode(children[0]);
        if (children[1]->kind != NodeKind::EmptyStmt &&
            CheckExpression(children[1]) != DataType::Bool()) Error(children[1], "condition must be bool");
        if (children[2]->kind != NodeKind::EmptyStmt) CheckExpression(children[2]);
        CheckNode(children[3]);
        scopes_.pop_back();
        break;
    }
    case NodeKind::DoWhileStmt: {
        const auto children = node->Children();
        CheckNode(children[0]);
        if (CheckExpression(children[1]) != DataType::Bool()) Error(children[1], "condition must be bool");
        break;
    }
    case NodeKind::SwitchStmt: {
        AstNode* selector = node->firstChild;
        if (CheckExpression(selector) != DataType::Int()) Error(selector, "switch expression must be int");
        std::unordered_set<std::int32_t> values;
        bool hasDefault = false;
        scopes_.emplace_back();
        for (AstNode* clause = selector ? selector->nextSibling : nullptr; clause;
             clause = clause->nextSibling) {
            AstNode* statement = clause->firstChild;
            if (clause->kind == NodeKind::CaseClause) {
                AstNode* valueExpression = statement;
                const DataType valueType = CheckExpression(valueExpression);
                auto value = ConstantExpressionEvaluator{}.Evaluate(valueExpression);
                if (valueType != DataType::Int() || !value || value->Type() != DataType::Int()) {
                    Error(valueExpression, "case value must be an integer constant expression");
                } else if (!values.insert(value->As<std::int32_t>()).second) {
                    Error(valueExpression, "duplicate case value");
                }
                statement = statement ? statement->nextSibling : nullptr;
            } else if (clause->kind == NodeKind::DefaultClause) {
                if (hasDefault) Error(clause, "duplicate default clause");
                hasDefault = true;
            }
            for (; statement; statement = statement->nextSibling) CheckNode(statement);
        }
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
    case NodeKind::ExprStmt: CheckExpression(node->firstChild); break;
    case NodeKind::ClassDecl: case NodeKind::InterfaceDecl: case NodeKind::EmptyStmt:
    case NodeKind::CaseClause: case NodeKind::DefaultClause: break;
    default: CheckExpression(node); break;
    }
}

void TypeChecker::CheckFunction(AstNode* node) {
    const DataType previousReturn = currentReturn_;
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
        case TokenKind::Integer: result = DataType::Int(); break;
        case TokenKind::Float: result = DataType::Float(); break;
        case TokenKind::String: result = DataType::String(); break;
        case TokenKind::KwTrue: case TokenKind::KwFalse: result = DataType::Bool(); break;
        case TokenKind::KwNull: result = DataType::Object("<null>", true); break;
        default: break;
        }
        break;
    case NodeKind::Identifier: {
        const auto type = Lookup(node->token.lexeme);
        if (!type) Error(node, "unknown variable '" + node->token.lexeme + "'");
        else result = type->type;
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
    case NodeKind::Binary: result = CheckBinary(node); break;
    case NodeKind::Unary: result = CheckUnary(node); break;
    case NodeKind::Call: result = CheckCall(node); break;
    case NodeKind::Assign: {
        const auto children = node->Children();
        if (children[0]->kind != NodeKind::Identifier && children[0]->kind != NodeKind::Member) {
            Error(children[0], "left side of assignment is not assignable");
        }
        if (IsReadOnlyLValue(children[0])) {
            Error(children[0], "cannot assign to const variable '" + children[0]->token.lexeme + "'");
        }
        DataType target = CheckExpression(children[0]);
        DataType value = CheckExpression(children[1]);
        if (!CanConvert(value, target)) Error(node, "cannot assign " + value.Name() + " to " + target.Name());
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
    if (op == TokenKind::EqualEqual || op == TokenKind::BangEqual || op == TokenKind::KwIs) {
        if (left != right && !(left.IsNumeric() && right.IsNumeric())) Error(node, "incomparable operand types");
        return DataType::Bool();
    }
    if (!left.IsNumeric() || !right.IsNumeric()) {
        Error(node, "operator requires numeric operands"); return DataType::Invalid();
    }
    if (op == TokenKind::Less || op == TokenKind::LessEqual ||
        op == TokenKind::Greater || op == TokenKind::GreaterEqual) return DataType::Bool();
    return left == DataType::Float() || right == DataType::Float() ? DataType::Float() : DataType::Int();
}

DataType TypeChecker::CheckUnary(AstNode* node) {
    DataType operand = CheckExpression(node->firstChild);
    if (node->token.kind == TokenKind::Bang) {
        if (operand != DataType::Bool()) Error(node, "'!' requires bool operand");
        return DataType::Bool();
    }
    if (node->token.kind == TokenKind::At) return operand;
    if (!operand.IsNumeric()) Error(node, "numeric unary operator requires numeric operand");
    return operand;
}

DataType TypeChecker::CheckCall(AstNode* node) {
    AstNode* callee = node->firstChild;
    if (!callee) return DataType::Invalid();
    if (callee->kind == NodeKind::Identifier) {
        if (const ClassSignature* type = FindClass(callee->token.lexeme)) {
            if (type->interfaceType || callee->nextSibling) {
                Error(node, "only zero-argument concrete class factories are supported");
                return DataType::Invalid();
            }
            return DataType::Object(type->name, true);
        }
    }
    if (callee->kind != NodeKind::Identifier) {
        Error(node, "method calls are not available yet"); return DataType::Invalid();
    }
    std::vector<DataType> arguments;
    for (AstNode* argument = callee->nextSibling; argument; argument = argument->nextSibling) {
        arguments.push_back(CheckExpression(argument));
    }
    const FunctionSignature* best = nullptr;
    int bestCost = 1000000;
    for (const auto& function : functions_) {
        if (function.name != callee->token.lexeme || function.parameters.size() != arguments.size()) continue;
        int cost = 0;
        bool viable = true;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (arguments[i] == function.parameters[i]) continue;
            if (arguments[i] == DataType::Int() && function.parameters[i] == DataType::Float()) ++cost;
            else viable = false;
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
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(std::string(name));
        if (found != scope->end()) return found->second;
    }
    return std::nullopt;
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
    for (const auto& type : classes_) if (type.name == name) return &type;
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
    if (from == DataType::Int() && to == DataType::Float()) return true;
    if (from.kind == TypeKind::Object && from.objectName == "<null>" && to.isHandle) return true;
    if (from.kind == TypeKind::Object && to.kind == TypeKind::Object && from.isHandle && to.isHandle) {
        if (const auto* type = FindClass(from.objectName)) {
            for (const auto& interfaceName : type->interfaces) if (interfaceName == to.objectName) return true;
        }
    }
    return false;
}

void TypeChecker::Error(const AstNode* node, std::string message) {
    diagnostics_.Report(node ? node->token.location : SourceLocation{}, Severity::Error, std::move(message));
}

} // namespace mini_as
