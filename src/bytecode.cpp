#include "mini_as/bytecode.hpp"
#include "mini_as/constant_evaluator.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace mini_as {
namespace {

std::string FunctionKey(const FunctionSignature& signature) {
    return (signature.method ? signature.objectType + "::" : std::string{}) +
        signature.Declaration();
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

AstNode* ArgumentExpression(AstNode* argument) {
    return argument && argument->kind == NodeKind::NamedArgument ? argument->firstChild : argument;
}

ParameterMode ParameterModeAt(const FunctionSignature& signature, std::size_t index) {
    return index < signature.parameterModes.size()
        ? signature.parameterModes[index] : ParameterMode::Value;
}

std::optional<std::vector<AstNode*>> OrderArguments(
    const FunctionSignature& signature, const std::vector<AstNode*>& arguments) {
    if (arguments.size() > signature.parameters.size()) return std::nullopt;
    std::vector<AstNode*> ordered(signature.parameters.size(), nullptr);
    std::size_t positional = 0;
    for (AstNode* argument : arguments) {
        std::size_t parameter = positional;
        if (argument->kind == NodeKind::NamedArgument) {
            parameter = signature.parameterNames.size();
            for (std::size_t index = 0; index < signature.parameterNames.size(); ++index) {
                if (signature.parameterNames[index] == argument->token.lexeme) { parameter = index; break; }
            }
            if (parameter >= signature.parameters.size()) return std::nullopt;
        } else {
            while (parameter < ordered.size() && ordered[parameter]) ++parameter;
            positional = parameter + 1;
        }
        if (parameter >= ordered.size() || ordered[parameter]) return std::nullopt;
        ordered[parameter] = ArgumentExpression(argument);
    }
    const std::size_t firstDefault = signature.parameters.size() - signature.defaultArgumentCount;
    for (std::size_t index = 0; index < ordered.size(); ++index)
        if (!ordered[index] && index < firstDefault) return std::nullopt;
    return ordered;
}

} // namespace
std::string_view OpCodeName(OpCode opcode) {
    static const char* names[] = {
        "NOP", "SUSPEND", "PUSH_CONST", "PUSH_VOID", "LOAD_LOCAL", "STORE_LOCAL",
        "LOAD_GLOBAL", "STORE_GLOBAL", "DUP", "SWAP", "POP",
        "TO_FLOAT", "TO_DOUBLE", "TO_INTEGER", "TO_STRING",
        "ADD_I", "SUB_I", "MUL_I", "DIV_I", "MOD_I", "POW_I",
        "BIT_AND", "BIT_OR", "BIT_XOR", "SHL", "SHR", "USHR",
        "ADD_F", "SUB_F", "MUL_F", "DIV_F", "POW_F",
        "ADD_D", "SUB_D", "MUL_D", "DIV_D", "POW_D",
        "CONCAT", "NEG_I", "NEG_F", "NEG_D", "BIT_NOT", "NOT",
        "EQ", "NE", "LT", "LE", "GT", "GE", "JMP", "JZ", "CALL", "CALL_HOST",
        "CALL_VIRTUAL", "CAST_OBJECT", "NEW_OBJECT", "LOAD_FIELD", "STORE_FIELD",
        "MAKE_GLOBAL_REF", "MAKE_FIELD_REF", "LOAD_REF", "STORE_REF", "RET"
    };
    return names[static_cast<std::size_t>(opcode)];
}

std::string Disassemble(const BytecodeFunction& function) {
    std::ostringstream out;
    out << function.signature.Declaration() << "\n";
    for (std::size_t i = 0; i < function.code.size(); ++i) {
        const auto& instruction = function.code[i];
        out << std::setw(4) << i << "  " << std::left << std::setw(12)
            << OpCodeName(instruction.opcode) << std::right;
        if (instruction.opcode == OpCode::PushConst || instruction.opcode == OpCode::LoadLocal ||
            instruction.opcode == OpCode::StoreLocal || instruction.opcode == OpCode::LoadGlobal ||
            instruction.opcode == OpCode::StoreGlobal || instruction.opcode == OpCode::Jump ||
            instruction.opcode == OpCode::ToInteger ||
            instruction.opcode == OpCode::JumpIfFalse || instruction.opcode == OpCode::Call ||
            instruction.opcode == OpCode::CallHost || instruction.opcode == OpCode::CallVirtual ||
            instruction.opcode == OpCode::CastObject ||
            instruction.opcode == OpCode::NewObject ||
            instruction.opcode == OpCode::LoadField || instruction.opcode == OpCode::StoreField ||
            instruction.opcode == OpCode::MakeGlobalReference ||
            instruction.opcode == OpCode::MakeFieldReference) out << instruction.operand;
        out << "  ; " << instruction.location.row << ':' << instruction.location.column << '\n';
    }
    return out.str();
}

const BytecodeFunction* BytecodeModule::FindFunction(FunctionId id) const {
    for (const auto& function : functions) if (function.signature.id == id) return &function;
    return nullptr;
}

const RegisteredHostFunction* BytecodeModule::FindHostFunction(FunctionId id) const {
    for (const auto& target : hostFunctions) if (target.first == id) return target.second;
    return nullptr;
}

const TypeInfo* BytecodeModule::FindType(TypeId id) const {
    for (const auto& target : objectTypes) if (target.first == id) return target.second;
    return nullptr;
}

FunctionId BytecodeModule::FindDestructor(TypeId id) const {
    for (const auto& target : destructors) if (target.first == id) return target.second;
    return {};
}

std::vector<FunctionId> BytecodeModule::FindDestructors(TypeId id) const {
    std::vector<FunctionId> result;
    for (const auto& target : destructors) if (target.first == id) result.push_back(target.second);
    return result;
}

const CallableRef* BytecodeModule::FindCallable(std::size_t index) const {
    return index < callables.size() ? &callables[index] : nullptr;
}

std::optional<std::size_t> BytecodeModule::FindGlobalIndex(GlobalId id) const {
    for (std::size_t index = 0; index < globals.size(); ++index)
        if (globals[index].signature.id == id) return index;
    return std::nullopt;
}

const BytecodeFunction* BytecodeModule::ResolveVirtual(
    TypeId concreteType, TypeId interfaceType, std::uint32_t slot) const {
    for (const auto& entry : virtualDispatch) {
        if (entry.concreteType == concreteType && entry.interfaceType == interfaceType &&
            entry.slot == slot) return FindFunction(entry.implementation);
    }
    return nullptr;
}

BytecodeCompiler::BytecodeCompiler(DiagnosticSink& diagnostics) : diagnostics_(diagnostics) {}

BytecodeModule BytecodeCompiler::Compile(AstNode* root, const std::vector<FunctionSignature>& signatures,
                                         const std::vector<ClassSignature>& classes,
                                         const std::vector<GlobalSignature>& globals,
                                         const std::vector<EnumSignature>& enums) {
    module_ = {};
    signatures_ = signatures;
    functionIndices_.clear();
    functionIds_.clear();
    hostIds_.clear();
    classes_ = classes;
    for (const auto& type : classes_)
        for (const auto& method : type.methods) signatures_.push_back(method);
    globals_ = globals;
    globalSymbols_.clear();
    for (const auto& global : globals_) {
        globalSymbols_[global.name] = global;
        module_.globals.push_back({global});
    }
    enumConstants_.clear();
    for (const auto& type : enums) {
        const DataType enumType = DataType::Enum(type.name);
        const std::string enumNamespace = NamespaceOf(type.name);
        for (const auto& value : type.values) {
            enumConstants_.emplace(
                enumNamespace.empty() ? value.name : enumNamespace + "::" + value.name,
                Value::Integer(enumType, static_cast<std::uint32_t>(value.value)));
        }
    }
    classIds_.clear();
    classNodes_.clear();
    functionNodes_.clear();
    std::uint32_t nextFunctionId = 0;
    for (const auto& signature : signatures_) {
        if (signature.id.IsValid() && signature.id.value >= nextFunctionId)
            nextFunctionId = signature.id.value + 1;
    }
    for (auto& signature : signatures_) {
        if (!signature.id.IsValid()) signature.id = FunctionId{nextFunctionId++};
    }
    std::uint32_t nextTypeId = 0;
    for (const auto& type : classes_) {
        if (type.id.IsValid() && type.id.value >= nextTypeId) nextTypeId = type.id.value + 1;
    }
    for (auto& type : classes_) {
        if (!type.id.IsValid()) type.id = TypeId{nextTypeId++};
        classIds_[type.name] = type.id;
        const ClassSignature* owner = &type;
        while (owner) {
            for (const auto& method : owner->methods) {
                if (!method.destructor) continue;
                for (const auto& signature : signatures_) {
                    if (FunctionKey(signature) == FunctionKey(method))
                        module_.destructors.push_back({type.id, signature.id});
                }
            }
            owner = owner->baseClass.empty() ? nullptr : FindClass(owner->baseClass);
        }
    }
    if (!root) return module_;
    for (AstNode* node : TopLevelDeclarations(root))
        if (node->kind == NodeKind::ClassDecl) classNodes_[node->token.lexeme] = node;
    for (AstNode* node : TopLevelDeclarations(root)) {
        if (node->kind == NodeKind::FunctionDecl) {
            FunctionSignature signature{node->token.lexeme, node->declaredType, {}, false, {}, {}, false, false, 0, {}, {},
                                        node->returnsReference, node->returnReferenceConst, false};
            for (AstNode* parameter = node->firstChild;
                 parameter && parameter->kind == NodeKind::Parameter; parameter = parameter->nextSibling) {
                signature.parameters.push_back(parameter->declaredType);
                signature.parameterNames.push_back(parameter->token.lexeme);
                signature.parameterModes.push_back(parameter->parameterMode);
                if (parameter->firstChild) ++signature.defaultArgumentCount;
            }
            functionNodes_[FunctionKey(signature)] = node;
        }
        if (node->kind == NodeKind::ClassDecl) {
            for (AstNode* methodNode = node->firstChild; methodNode; methodNode = methodNode->nextSibling) {
                if (methodNode->kind != NodeKind::FunctionDecl) continue;
                FunctionSignature method{methodNode->token.lexeme, methodNode->declaredType, {}, false, {},
                                         node->token.lexeme, true, methodNode->isConstructor, 0, {}, {},
                                         methodNode->returnsReference, methodNode->returnReferenceConst,
                                         methodNode->isDestructor};
                for (AstNode* parameter = methodNode->firstChild;
                     parameter && parameter->kind == NodeKind::Parameter; parameter = parameter->nextSibling) {
                    method.parameters.push_back(parameter->declaredType);
                    method.parameterNames.push_back(parameter->token.lexeme);
                    method.parameterModes.push_back(parameter->parameterMode);
                    if (parameter->firstChild) ++method.defaultArgumentCount;
                }
                functionNodes_[FunctionKey(method)] = methodNode;
            }
        }
    }
    for (const auto& signature : signatures_) {
        if (signature.host) {
            hostIds_[FunctionKey(signature)] = signature.id;
            continue;
        }
        bool interfaceMethod = false;
        if (signature.method) {
            for (const auto& type : classes_)
                if (type.name == signature.objectType) interfaceMethod = type.interfaceType;
        }
        if (interfaceMethod) continue;
        const auto index = module_.functions.size();
        BytecodeFunction function;
        function.signature = signature;
        module_.functions.push_back(std::move(function));
        functionIndices_[FunctionKey(signature)] = index;
        functionIds_[FunctionKey(signature)] = signature.id;
    }
    for (const auto& concrete : classes_) {
        if (concrete.interfaceType) continue;
        for (const auto& interfaceName : concrete.interfaces) {
            const ClassSignature* interfaceType = nullptr;
            for (const auto& candidate : classes_)
                if (candidate.name == interfaceName && candidate.interfaceType) interfaceType = &candidate;
            if (!interfaceType) continue;
            for (std::size_t slot = 0; slot < interfaceType->methods.size(); ++slot) {
                const auto& required = interfaceType->methods[slot];
                const FunctionSignature* implementation = FindClassMethod(concrete, required);
                if (!implementation) continue;
                const auto found = functionIds_.find(FunctionKey(*implementation));
                if (found != functionIds_.end())
                    module_.virtualDispatch.push_back({concrete.id, interfaceType->id,
                                                       static_cast<std::uint32_t>(slot), found->second});
            }
        }
    }
    for (const auto& concrete : classes_) {
        if (concrete.interfaceType) continue;
        const auto concreteLayout = VirtualLayout(concrete);
        for (const ClassSignature* staticType = &concrete; staticType;
             staticType = staticType->baseClass.empty() ? nullptr : FindClass(staticType->baseClass)) {
            const auto staticLayout = VirtualLayout(*staticType);
            for (std::size_t slot = 0; slot < staticLayout.size() && slot < concreteLayout.size(); ++slot) {
                const FunctionSignature* implementation = concreteLayout[slot];
                const auto found = functionIds_.find(FunctionKey(*implementation));
                if (found != functionIds_.end())
                    module_.virtualDispatch.push_back({concrete.id, staticType->id,
                                                       static_cast<std::uint32_t>(slot), found->second});
            }
        }
    }
    for (AstNode* node : TopLevelDeclarations(root)) {
        if (node->kind != NodeKind::FunctionDecl) continue;
        FunctionSignature astSignature{node->token.lexeme, node->declaredType, {}, false, {}, {}, false, false, 0, {}, {},
                                       node->returnsReference, node->returnReferenceConst, false};
        for (AstNode* child = node->firstChild; child && child->kind == NodeKind::Parameter; child = child->nextSibling) {
            astSignature.parameters.push_back(child->declaredType);
            astSignature.parameterModes.push_back(child->parameterMode);
        }
        const auto found = functionIndices_.find(FunctionKey(astSignature));
        if (found != functionIndices_.end()) CompileFunction(node, found->second);
    }
    for (AstNode* typeNode : TopLevelDeclarations(root)) {
        if (typeNode->kind != NodeKind::ClassDecl) continue;
        for (AstNode* methodNode = typeNode->firstChild; methodNode; methodNode = methodNode->nextSibling) {
            if (methodNode->kind != NodeKind::FunctionDecl || !methodNode->firstChild) continue;
            FunctionSignature method{methodNode->token.lexeme, methodNode->declaredType, {}, false, {},
                                     typeNode->token.lexeme, true, methodNode->isConstructor, 0, {}, {},
                                     methodNode->returnsReference, methodNode->returnReferenceConst,
                                     methodNode->isDestructor};
            for (AstNode* parameter = methodNode->firstChild;
                 parameter && parameter->kind == NodeKind::Parameter; parameter = parameter->nextSibling) {
                method.parameters.push_back(parameter->declaredType);
                method.parameterModes.push_back(parameter->parameterMode);
            }
            const auto found = functionIndices_.find(FunctionKey(method));
            if (found != functionIndices_.end())
                CompileFunction(methodNode, found->second, typeNode->token.lexeme);
        }
    }
    CompileGlobalInitializer(root);
    return std::move(module_);
}

void BytecodeCompiler::CompileGlobalInitializer(AstNode* root) {
    function_ = &module_.globalInitializer;
    function_->signature.name = "$globals";
    function_->signature.returnType = DataType::Void();
    function_->code.clear();
    function_->constants.clear();
    scopes_.clear();
    scopes_.emplace_back();
    controlFlow_.clear();
    nextLocal_ = 0;
    auto compileDeclaration = [&](AstNode* declaration) {
        if (!declaration || !declaration->isGlobal || !declaration->firstChild) return;
        const auto found = globalSymbols_.find(declaration->token.lexeme);
        if (found == globalSymbols_.end()) return;
        currentNamespace_ = NamespaceOf(declaration->token.lexeme);
        CompileExpression(declaration->firstChild);
        EmitConversion(declaration->firstChild->inferredType, declaration->declaredType, declaration);
        Emit(OpCode::StoreGlobal, static_cast<std::int32_t>(found->second.id.value), declaration);
    };
    if (root) {
        for (AstNode* node : TopLevelDeclarations(root)) {
            if (node->kind == NodeKind::VarDecl) compileDeclaration(node);
            if (node->kind == NodeKind::DeclList) {
                for (AstNode* declaration = node->firstChild; declaration;
                     declaration = declaration->nextSibling) compileDeclaration(declaration);
            }
        }
    }
    Emit(OpCode::PushVoid, 0, root);
    Emit(OpCode::Return, 0, root);
    function_->localCount = static_cast<std::size_t>(nextLocal_);
    currentNamespace_.clear();
    function_ = nullptr;
}

void BytecodeCompiler::CompileFunction(AstNode* node, std::size_t functionIndex,
                                       std::string objectType) {
    function_ = &module_.functions.at(functionIndex);
    function_->code.clear();
    function_->constants.clear();
    scopes_.clear();
    scopes_.emplace_back();
    controlFlow_.clear();
    currentObjectType_ = std::move(objectType);
    currentNamespace_ = NamespaceOf(currentObjectType_.empty()
        ? function_->signature.name : currentObjectType_);
    implicitThisSlot_ = 0;
    nextLocal_ = 0;
    if (!currentObjectType_.empty()) {
        Token thisToken{TokenKind::Identifier, "this", node->token.location};
        DeclareLocal(thisToken);
    }
    AstNode* child = node->firstChild;
    while (child && child->kind == NodeKind::Parameter) {
        DeclareLocal(child->token);
        child = child->nextSibling;
    }
    if (function_->signature.constructor && !node->hasExplicitSuper)
        CompileImplicitBaseConstructor(currentObjectType_, node);
    if (child && child->kind == NodeKind::Block) CompileBlock(child, false);
    if (function_->code.empty() || function_->code.back().opcode != OpCode::Return) {
        Emit(OpCode::PushVoid, 0, node);
        Emit(OpCode::Return, 0, node);
    }
    function_->localCount = static_cast<std::size_t>(nextLocal_);
    currentObjectType_.clear();
    currentNamespace_.clear();
    function_ = nullptr;
}

void BytecodeCompiler::CompileBlock(AstNode* node, bool createScope) {
    if (createScope) scopes_.emplace_back();
    for (AstNode* child = node->firstChild; child; child = child->nextSibling) CompileStatement(child);
    if (createScope) scopes_.pop_back();
}

void BytecodeCompiler::CompileStatement(AstNode* node) {
    Emit(OpCode::Suspend, 0, node);
    switch (node->kind) {
    case NodeKind::Block: CompileBlock(node); break;
    case NodeKind::DeclList:
        for (AstNode* declaration = node->firstChild; declaration; declaration = declaration->nextSibling)
            CompileStatement(declaration);
        break;
    case NodeKind::VarDecl: {
        const auto slot = DeclareLocal(node->token);
        if (node->firstChild) CompileExpression(node->firstChild);
        else Emit(OpCode::PushVoid, 0, node);
        if (node->firstChild) EmitConversion(node->firstChild->inferredType, node->declaredType, node);
        Emit(OpCode::StoreLocal, static_cast<std::int32_t>(slot.value), node);
        break;
    }
    case NodeKind::ExprStmt: CompileExpression(node->firstChild); Emit(OpCode::Pop, 0, node); break;
    case NodeKind::ReturnStmt:
        if (node->firstChild && function_->signature.returnsReference)
            CompileReferenceTarget(node->firstChild, node);
        else if (node->firstChild) CompileExpression(node->firstChild);
        else Emit(OpCode::PushVoid, 0, node);
        if (node->firstChild && !function_->signature.returnsReference)
            EmitConversion(node->firstChild->inferredType, function_->signature.returnType, node);
        Emit(OpCode::Return, 0, node);
        break;
    case NodeKind::IfStmt: {
        const auto children = node->Children();
        CompileExpression(children[0]);
        const auto elseJump = Emit(OpCode::JumpIfFalse, -1, node);
        CompileStatement(children[1]);
        if (children.size() == 3) {
            const auto endJump = Emit(OpCode::Jump, -1, node);
            PatchJump(elseJump, function_->code.size());
            CompileStatement(children[2]);
            PatchJump(endJump, function_->code.size());
        } else PatchJump(elseJump, function_->code.size());
        break;
    }
    case NodeKind::WhileStmt: {
        const auto children = node->Children();
        const auto loopStart = function_->code.size();
        CompileExpression(children[0]);
        const auto exitJump = Emit(OpCode::JumpIfFalse, -1, node);
        controlFlow_.push_back({{}, {}, true, loopStart});
        CompileStatement(children[1]);
        Emit(OpCode::Jump, static_cast<std::int32_t>(loopStart), node);
        PatchJump(exitJump, function_->code.size());
        for (const auto jump : controlFlow_.back().breakJumps) PatchJump(jump, function_->code.size());
        for (const auto jump : controlFlow_.back().continueJumps)
            PatchJump(jump, controlFlow_.back().continueTarget);
        controlFlow_.pop_back();
        break;
    }
    case NodeKind::ForStmt: {
        const auto children = node->Children();
        scopes_.emplace_back();
        if (children[0]->kind != NodeKind::EmptyStmt) CompileStatement(children[0]);
        const auto condition = function_->code.size();
        if (children[1]->kind == NodeKind::EmptyStmt)
            Emit(OpCode::PushConst, AddConstant(Value(true)), node);
        else CompileExpression(children[1]);
        const auto exitJump = Emit(OpCode::JumpIfFalse, -1, node);
        controlFlow_.push_back({{}, {}, true, condition});
        CompileStatement(children[3]);
        controlFlow_.back().continueTarget = function_->code.size();
        if (children[2]->kind != NodeKind::EmptyStmt) {
            CompileExpression(children[2]);
            Emit(OpCode::Pop, 0, children[2]);
        }
        Emit(OpCode::Jump, static_cast<std::int32_t>(condition), node);
        PatchJump(exitJump, function_->code.size());
        for (const auto jump : controlFlow_.back().breakJumps) PatchJump(jump, function_->code.size());
        for (const auto jump : controlFlow_.back().continueJumps)
            PatchJump(jump, controlFlow_.back().continueTarget);
        controlFlow_.pop_back();
        scopes_.pop_back();
        break;
    }
    case NodeKind::DoWhileStmt: {
        const auto children = node->Children();
        const auto body = function_->code.size();
        controlFlow_.push_back({{}, {}, true, 0});
        CompileStatement(children[0]);
        controlFlow_.back().continueTarget = function_->code.size();
        CompileExpression(children[1]);
        const auto exitJump = Emit(OpCode::JumpIfFalse, -1, node);
        Emit(OpCode::Jump, static_cast<std::int32_t>(body), node);
        PatchJump(exitJump, function_->code.size());
        for (const auto jump : controlFlow_.back().breakJumps) PatchJump(jump, function_->code.size());
        for (const auto jump : controlFlow_.back().continueJumps)
            PatchJump(jump, controlFlow_.back().continueTarget);
        controlFlow_.pop_back();
        break;
    }
    case NodeKind::SwitchStmt: {
        AstNode* selector = node->firstChild;
        scopes_.emplace_back();
        const VariableId selectorSlot = DeclareLocal(node->token);
        CompileExpression(selector);
        Emit(OpCode::StoreLocal, static_cast<std::int32_t>(selectorSlot.value), node);
        std::vector<std::pair<AstNode*, std::size_t>> caseJumps;
        AstNode* defaultClause = nullptr;
        for (AstNode* clause = selector ? selector->nextSibling : nullptr; clause;
             clause = clause->nextSibling) {
            if (clause->kind == NodeKind::DefaultClause) {
                if (!defaultClause) defaultClause = clause;
                continue;
            }
            AstNode* valueExpression = clause->firstChild;
            ConstantExpressionEvaluator evaluator([this](std::string_view name) -> std::optional<Value> {
                for (const auto& candidate : NameCandidates(currentNamespace_, name)) {
                    const auto found = enumConstants_.find(candidate);
                    if (found != enumConstants_.end()) return found->second;
                }
                return std::nullopt;
            });
            auto value = evaluator.Evaluate(valueExpression);
            if (!value || !value->Type().IsInteger()) {
                Error(valueExpression, "case value cannot be compiled");
                continue;
            }
            Emit(OpCode::LoadLocal, static_cast<std::int32_t>(selectorSlot.value), clause);
            Emit(OpCode::PushConst, AddConstant(ConvertInteger(*value, selector->inferredType)), valueExpression);
            Emit(OpCode::Equal, 0, clause);
            const auto nextComparison = Emit(OpCode::JumpIfFalse, -1, clause);
            caseJumps.push_back({clause, Emit(OpCode::Jump, -1, clause)});
            PatchJump(nextComparison, function_->code.size());
        }
        const auto defaultJump = Emit(OpCode::Jump, -1, node);
        controlFlow_.push_back({{}, {}, false, 0});
        for (AstNode* clause = selector ? selector->nextSibling : nullptr; clause;
             clause = clause->nextSibling) {
            for (const auto& pending : caseJumps)
                if (pending.first == clause) PatchJump(pending.second, function_->code.size());
            if (clause == defaultClause) PatchJump(defaultJump, function_->code.size());
            AstNode* statement = clause->firstChild;
            if (clause->kind == NodeKind::CaseClause && statement) statement = statement->nextSibling;
            for (; statement; statement = statement->nextSibling) CompileStatement(statement);
        }
        if (!defaultClause) PatchJump(defaultJump, function_->code.size());
        for (const auto jump : controlFlow_.back().breakJumps) PatchJump(jump, function_->code.size());
        controlFlow_.pop_back();
        scopes_.pop_back();
        break;
    }
    case NodeKind::BreakStmt:
        if (controlFlow_.empty()) Error(node, "break target is unavailable");
        else controlFlow_.back().breakJumps.push_back(Emit(OpCode::Jump, -1, node));
        break;
    case NodeKind::ContinueStmt: {
        auto target = controlFlow_.rend();
        for (auto context = controlFlow_.rbegin(); context != controlFlow_.rend(); ++context) {
            if (context->loop) { target = context; break; }
        }
        if (target == controlFlow_.rend()) Error(node, "continue target is unavailable");
        else target->continueJumps.push_back(Emit(OpCode::Jump, -1, node));
        break;
    }
    default: Error(node, "statement cannot be compiled"); break;
    }
}

void BytecodeCompiler::CompileExpression(AstNode* node) {
    if (!node) return;
    switch (node->kind) {
    case NodeKind::Literal: {
        auto value = ConstantExpressionEvaluator{}.Evaluate(node);
        if (!value) { Error(node, "literal cannot be compiled"); return; }
        Emit(OpCode::PushConst, AddConstant(std::move(*value)), node);
        break;
    }
    case NodeKind::Identifier: {
        const auto target = ResolveLValue(node);
        if (target) CompileLValueLoad(*target, node);
        else {
            const Value* constant = nullptr;
            for (const auto& candidate : NameCandidates(currentNamespace_, node->token.lexeme)) {
                const auto found = enumConstants_.find(candidate);
                if (found != enumConstants_.end()) { constant = &found->second; break; }
            }
            if (!constant) Error(node, "unknown local '" + node->token.lexeme + "'");
            else Emit(OpCode::PushConst, AddConstant(*constant), node);
        }
        break;
    }
    case NodeKind::Assign: {
        const auto children = node->Children();
        const auto target = ResolveLValue(children[0]);
        if (!target) Error(node, "unknown assignment target");
        else if (node->token.kind == TokenKind::Equal) CompileLValueStore(*target, children[1], node);
        else CompileCompoundAssignment(*target, children[1], node->token.kind, node);
        break;
    }
    case NodeKind::Member: {
        const auto target = ResolveLValue(node);
        if (!target) Error(node, "unknown field");
        else CompileLValueLoad(*target, node);
        break;
    }
    case NodeKind::Conditional: {
        const auto children = node->Children();
        CompileExpression(children[0]);
        const auto elseJump = Emit(OpCode::JumpIfFalse, -1, node);
        CompileExpression(children[1]);
        EmitConversion(children[1]->inferredType, node->inferredType, children[1]);
        const auto endJump = Emit(OpCode::Jump, -1, node);
        PatchJump(elseJump, function_->code.size());
        CompileExpression(children[2]);
        EmitConversion(children[2]->inferredType, node->inferredType, children[2]);
        PatchJump(endJump, function_->code.size());
        break;
    }
    case NodeKind::Binary:
        if (node->token.kind == TokenKind::AndAnd || node->token.kind == TokenKind::OrOr) CompileLogical(node);
        else CompileBinary(node);
        break;
    case NodeKind::Unary:
        CompileExpression(node->firstChild);
        if (node->token.kind == TokenKind::Bang) Emit(OpCode::LogicalNot, 0, node);
        else if (node->token.kind == TokenKind::Tilde) Emit(OpCode::BitNot, 0, node);
        else if (node->token.kind == TokenKind::Minus) {
            Emit(node->inferredType == DataType::Double() ? OpCode::NegDouble
                 : node->inferredType == DataType::Float() ? OpCode::NegFloat
                                                           : OpCode::NegInt, 0, node);
        }
        break;
    case NodeKind::Increment: CompileIncrement(node); break;
    case NodeKind::Cast: {
        CompileExpression(node->firstChild);
        const auto target = classIds_.find(node->inferredType.objectName);
        if (target == classIds_.end()) Error(node, "reference cast target is unavailable");
        else Emit(OpCode::CastObject, static_cast<std::int32_t>(target->second.value), node);
        break;
    }
    case NodeKind::Call: CompileCall(node); break;
    default: Error(node, "expression cannot be compiled"); break;
    }
}

std::optional<BytecodeCompiler::LValueRef> BytecodeCompiler::ResolveLValue(AstNode* expression) const {
    if (!expression) return std::nullopt;
    if (expression->kind == NodeKind::Identifier) {
        const auto local = LookupLocal(expression->token.lexeme);
        if (local) {
            LValueRef result;
            result.kind = LValueRef::Kind::Local;
            result.type = expression->inferredType;
            result.variable = *local;
            return result;
        }
        if (expression->implicitThis) {
            for (const auto& type : classes_) {
                if (type.name != currentObjectType_) continue;
                for (std::size_t index = 0; index < type.fields.size(); ++index) {
                    if (type.fields[index].name != expression->token.lexeme) continue;
                    LValueRef result;
                    result.kind = LValueRef::Kind::Field;
                    result.type = type.fields[index].type;
                    result.field = static_cast<std::uint32_t>(index);
                    return result;
                }
            }
        }
        for (const auto& candidate : NameCandidates(currentNamespace_, expression->token.lexeme)) {
            const auto global = globalSymbols_.find(candidate);
            if (global == globalSymbols_.end()) continue;
            LValueRef result;
            result.kind = LValueRef::Kind::Global;
            result.type = global->second.type;
            result.global = global->second.id;
            return result;
        }
        return std::nullopt;
    }
    if (expression->kind == NodeKind::Member) {
        const auto field = FindField(expression);
        if (!field) return std::nullopt;
        LValueRef result;
        result.kind = LValueRef::Kind::Field;
        result.type = field->second;
        result.field = static_cast<std::uint32_t>(field->first);
        result.receiver = expression->firstChild;
        return result;
    }
    if (expression->kind == NodeKind::Call && expression->returnsReference) {
        LValueRef result;
        result.kind = LValueRef::Kind::Dynamic;
        result.type = expression->inferredType;
        result.receiver = expression;
        return result;
    }
    return std::nullopt;
}

void BytecodeCompiler::CompileLValueLoad(const LValueRef& target, const AstNode* source) {
    switch (target.kind) {
    case LValueRef::Kind::Local:
        Emit(OpCode::LoadLocal, static_cast<std::int32_t>(target.variable.value), source);
        break;
    case LValueRef::Kind::Field:
        if (target.receiver) CompileExpression(target.receiver);
        else Emit(OpCode::LoadLocal, static_cast<std::int32_t>(implicitThisSlot_), source);
        Emit(OpCode::LoadField, static_cast<std::int32_t>(target.field), source);
        break;
    case LValueRef::Kind::Global:
        Emit(OpCode::LoadGlobal, static_cast<std::int32_t>(target.global.value), source);
        break;
    case LValueRef::Kind::Dynamic:
        CompileCall(target.receiver, false);
        Emit(OpCode::LoadReference, 0, source);
        break;
    case LValueRef::Kind::Index:
        Error(source, "lvalue kind is not implemented");
        break;
    }
}

void BytecodeCompiler::CompileLValueStore(const LValueRef& target, AstNode* value,
                                          const AstNode* source) {
    if (target.kind == LValueRef::Kind::Dynamic) {
        CompileCall(target.receiver, false);
        CompileExpression(value);
        EmitConversion(value->inferredType, target.type, source);
        Emit(OpCode::StoreReference, 0, source);
        return;
    }
    if (target.kind == LValueRef::Kind::Field) {
        if (target.receiver) CompileExpression(target.receiver);
        else Emit(OpCode::LoadLocal, static_cast<std::int32_t>(implicitThisSlot_), source);
    }
    CompileExpression(value);
    EmitConversion(value->inferredType, target.type, source);
    switch (target.kind) {
    case LValueRef::Kind::Local:
        Emit(OpCode::Dup, 0, source);
        Emit(OpCode::StoreLocal, static_cast<std::int32_t>(target.variable.value), source);
        break;
    case LValueRef::Kind::Field:
        Emit(OpCode::StoreField, static_cast<std::int32_t>(target.field), source);
        break;
    case LValueRef::Kind::Global:
        Emit(OpCode::Dup, 0, source);
        Emit(OpCode::StoreGlobal, static_cast<std::int32_t>(target.global.value), source);
        break;
    case LValueRef::Kind::Dynamic: break;
    case LValueRef::Kind::Index:
        Error(source, "lvalue kind is not implemented");
        break;
    }
}

void BytecodeCompiler::CompileCompoundAssignment(const LValueRef& target, AstNode* value,
                                                 TokenKind operation, const AstNode* source) {
    if (target.kind == LValueRef::Kind::Dynamic) {
        CompileCall(target.receiver, false);
        Emit(OpCode::Dup, 0, source);
        Emit(OpCode::LoadReference, 0, source);
    } else if (target.kind == LValueRef::Kind::Field) {
        if (target.receiver) CompileExpression(target.receiver);
        else Emit(OpCode::LoadLocal, static_cast<std::int32_t>(implicitThisSlot_), source);
        Emit(OpCode::Dup, 0, source);
        Emit(OpCode::LoadField, static_cast<std::int32_t>(target.field), source);
    } else {
        CompileLValueLoad(target, source);
    }
    const bool stringConcat = operation == TokenKind::PlusEqual && target.type == DataType::String();
    const bool bitwise = operation == TokenKind::AmpEqual || operation == TokenKind::PipeEqual ||
        operation == TokenKind::CaretEqual || operation == TokenKind::ShiftLeftEqual ||
        operation == TokenKind::ShiftRightEqual || operation == TokenKind::ShiftRightArithmeticEqual;
    const DataType operationType = stringConcat ? DataType::String()
        : bitwise ? target.type : CommonNumericType(target.type, value->inferredType);
    const bool floating = operationType == DataType::Float();
    const bool doublePrecision = operationType == DataType::Double();
    if (!stringConcat) EmitConversion(target.type, operationType, source);
    CompileExpression(value);
    if (stringConcat && value->inferredType != DataType::String()) Emit(OpCode::ToString, 0, source);
    if (!stringConcat) EmitConversion(value->inferredType, operationType, source);
    OpCode opcode = OpCode::Nop;
    if (operation == TokenKind::PlusEqual)
        opcode = stringConcat ? OpCode::Concat : (doublePrecision ? OpCode::AddDouble
                                               : floating ? OpCode::AddFloat : OpCode::AddInt);
    else if (operation == TokenKind::MinusEqual)
        opcode = doublePrecision ? OpCode::SubDouble : floating ? OpCode::SubFloat : OpCode::SubInt;
    else if (operation == TokenKind::StarEqual)
        opcode = doublePrecision ? OpCode::MulDouble : floating ? OpCode::MulFloat : OpCode::MulInt;
    else if (operation == TokenKind::SlashEqual)
        opcode = doublePrecision ? OpCode::DivDouble : floating ? OpCode::DivFloat : OpCode::DivInt;
    else if (operation == TokenKind::PercentEqual) opcode = OpCode::ModInt;
    else if (operation == TokenKind::StarStarEqual)
        opcode = doublePrecision ? OpCode::PowDouble : floating ? OpCode::PowFloat : OpCode::PowInt;
    else if (operation == TokenKind::AmpEqual) opcode = OpCode::BitAnd;
    else if (operation == TokenKind::PipeEqual) opcode = OpCode::BitOr;
    else if (operation == TokenKind::CaretEqual) opcode = OpCode::BitXor;
    else if (operation == TokenKind::ShiftLeftEqual) opcode = OpCode::ShiftLeft;
    else if (operation == TokenKind::ShiftRightEqual) opcode = OpCode::ShiftRight;
    else if (operation == TokenKind::ShiftRightArithmeticEqual) opcode = OpCode::ShiftRightArithmetic;
    else { Error(source, "compound assignment operator cannot be compiled"); return; }
    Emit(opcode, 0, source);
    if (!stringConcat) EmitConversion(operationType, target.type, source);
    switch (target.kind) {
    case LValueRef::Kind::Local:
        Emit(OpCode::Dup, 0, source);
        Emit(OpCode::StoreLocal, static_cast<std::int32_t>(target.variable.value), source);
        break;
    case LValueRef::Kind::Global:
        Emit(OpCode::Dup, 0, source);
        Emit(OpCode::StoreGlobal, static_cast<std::int32_t>(target.global.value), source);
        break;
    case LValueRef::Kind::Field:
        Emit(OpCode::StoreField, static_cast<std::int32_t>(target.field), source);
        break;
    case LValueRef::Kind::Dynamic:
        Emit(OpCode::StoreReference, 0, source);
        break;
    case LValueRef::Kind::Index:
        Error(source, "lvalue kind is not implemented");
        break;
    }
}

void BytecodeCompiler::CompileIncrement(AstNode* node) {
    const auto target = ResolveLValue(node ? node->firstChild : nullptr);
    if (!target) { Error(node, "increment target cannot be compiled"); return; }
    if (target->kind == LValueRef::Kind::Dynamic) {
        CompileCall(target->receiver, false);
        Emit(OpCode::Dup, 0, node);
        Emit(OpCode::LoadReference, 0, node);
        std::optional<VariableId> original;
        if (node->isPostfix) {
            original = VariableId{nextLocal_++};
            Emit(OpCode::Dup, 0, node);
            Emit(OpCode::StoreLocal, static_cast<std::int32_t>(original->value), node);
        }
        const bool floating = target->type == DataType::Float();
        const bool doublePrecision = target->type == DataType::Double();
        Emit(OpCode::PushConst,
             AddConstant(doublePrecision ? Value(1.0)
                         : floating ? Value(1.0f) : Value::Integer(target->type, 1)), node);
        Emit(node->token.kind == TokenKind::PlusPlus
                 ? (doublePrecision ? OpCode::AddDouble
                                    : floating ? OpCode::AddFloat : OpCode::AddInt)
                 : (doublePrecision ? OpCode::SubDouble
                                    : floating ? OpCode::SubFloat : OpCode::SubInt), 0, node);
        Emit(OpCode::StoreReference, 0, node);
        if (original) {
            Emit(OpCode::Pop, 0, node);
            Emit(OpCode::LoadLocal, static_cast<std::int32_t>(original->value), node);
        }
        return;
    }
    if (target->kind == LValueRef::Kind::Field) {
        if (target->receiver) CompileExpression(target->receiver);
        else Emit(OpCode::LoadLocal, static_cast<std::int32_t>(implicitThisSlot_), node);
        Emit(OpCode::Dup, 0, node);
        Emit(OpCode::LoadField, static_cast<std::int32_t>(target->field), node);
        if (node->isPostfix) {
            Emit(OpCode::Swap, 0, node);
            Emit(OpCode::Dup, 0, node);
            Emit(OpCode::LoadField, static_cast<std::int32_t>(target->field), node);
        }
    } else {
        CompileLValueLoad(*target, node);
        if (node->isPostfix) Emit(OpCode::Dup, 0, node);
    }
    const bool floating = target->type == DataType::Float();
    const bool doublePrecision = target->type == DataType::Double();
    Emit(OpCode::PushConst,
         AddConstant(doublePrecision ? Value(1.0)
                     : floating ? Value(1.0f) : Value::Integer(target->type, 1)), node);
    Emit(node->token.kind == TokenKind::PlusPlus
             ? (doublePrecision ? OpCode::AddDouble : floating ? OpCode::AddFloat : OpCode::AddInt)
             : (doublePrecision ? OpCode::SubDouble : floating ? OpCode::SubFloat : OpCode::SubInt),
         0, node);
    switch (target->kind) {
    case LValueRef::Kind::Local:
        Emit(OpCode::Dup, 0, node);
        Emit(OpCode::StoreLocal, static_cast<std::int32_t>(target->variable.value), node);
        break;
    case LValueRef::Kind::Global:
        Emit(OpCode::Dup, 0, node);
        Emit(OpCode::StoreGlobal, static_cast<std::int32_t>(target->global.value), node);
        break;
    case LValueRef::Kind::Field:
        Emit(OpCode::StoreField, static_cast<std::int32_t>(target->field), node);
        break;
    case LValueRef::Kind::Dynamic: break;
    case LValueRef::Kind::Index:
        Error(node, "lvalue kind is not implemented");
        return;
    }
    if (node->isPostfix) Emit(OpCode::Pop, 0, node);
}

void BytecodeCompiler::CompileBinary(AstNode* node) {
    const auto children = node->Children();
    const DataType result = node->inferredType;
    const bool stringOperation = node->token.kind == TokenKind::Plus && result == DataType::String();
    const bool bitwise = node->token.kind == TokenKind::Amp || node->token.kind == TokenKind::Pipe ||
        node->token.kind == TokenKind::Caret || node->token.kind == TokenKind::ShiftLeft ||
        node->token.kind == TokenKind::ShiftRight || node->token.kind == TokenKind::ShiftRightArithmetic;
    const DataType operationType = children[0]->inferredType.IsNumeric() &&
                                   children[1]->inferredType.IsNumeric()
        ? (bitwise ? children[0]->inferredType
                   : CommonNumericType(children[0]->inferredType, children[1]->inferredType))
        : result;
    CompileExpression(children[0]);
    if (!stringOperation) EmitConversion(children[0]->inferredType, operationType, node);
    if (stringOperation && children[0]->inferredType != DataType::String()) Emit(OpCode::ToString, 0, node);
    CompileExpression(children[1]);
    if (!stringOperation) EmitConversion(children[1]->inferredType, operationType, node);
    if (stringOperation && children[1]->inferredType != DataType::String()) Emit(OpCode::ToString, 0, node);
    const bool floating = operationType == DataType::Float();
    const bool doublePrecision = operationType == DataType::Double();
    OpCode opcode = OpCode::Nop;
    switch (node->token.kind) {
    case TokenKind::Plus: opcode = result == DataType::String() ? OpCode::Concat
        : doublePrecision ? OpCode::AddDouble : floating ? OpCode::AddFloat : OpCode::AddInt; break;
    case TokenKind::Minus: opcode = doublePrecision ? OpCode::SubDouble
        : floating ? OpCode::SubFloat : OpCode::SubInt; break;
    case TokenKind::Star: opcode = doublePrecision ? OpCode::MulDouble
        : floating ? OpCode::MulFloat : OpCode::MulInt; break;
    case TokenKind::Slash: opcode = doublePrecision ? OpCode::DivDouble
        : floating ? OpCode::DivFloat : OpCode::DivInt; break;
    case TokenKind::Percent: opcode = OpCode::ModInt; break;
    case TokenKind::StarStar: opcode = doublePrecision ? OpCode::PowDouble
        : floating ? OpCode::PowFloat : OpCode::PowInt; break;
    case TokenKind::Amp: opcode = OpCode::BitAnd; break;
    case TokenKind::Pipe: opcode = OpCode::BitOr; break;
    case TokenKind::Caret: opcode = OpCode::BitXor; break;
    case TokenKind::ShiftLeft: opcode = OpCode::ShiftLeft; break;
    case TokenKind::ShiftRight: opcode = OpCode::ShiftRight; break;
    case TokenKind::ShiftRightArithmetic: opcode = OpCode::ShiftRightArithmetic; break;
    case TokenKind::EqualEqual: case TokenKind::KwIs: opcode = OpCode::Equal; break;
    case TokenKind::BangEqual: opcode = OpCode::NotEqual; break;
    case TokenKind::Less: opcode = OpCode::Less; break;
    case TokenKind::LessEqual: opcode = OpCode::LessEqual; break;
    case TokenKind::Greater: opcode = OpCode::Greater; break;
    case TokenKind::GreaterEqual: opcode = OpCode::GreaterEqual; break;
    default: Error(node, "binary operator cannot be compiled"); return;
    }
    Emit(opcode, 0, node);
}

void BytecodeCompiler::CompileLogical(AstNode* node) {
    const auto children = node->Children();
    CompileExpression(children[0]);
    const auto branch = Emit(OpCode::JumpIfFalse, -1, node);
    if (node->token.kind == TokenKind::AndAnd) {
        CompileExpression(children[1]);
        const auto end = Emit(OpCode::Jump, -1, node);
        PatchJump(branch, function_->code.size());
        Emit(OpCode::PushConst, AddConstant(Value(false)), node);
        PatchJump(end, function_->code.size());
    } else {
        Emit(OpCode::PushConst, AddConstant(Value(true)), node);
        const auto end = Emit(OpCode::Jump, -1, node);
        PatchJump(branch, function_->code.size());
        CompileExpression(children[1]);
        PatchJump(end, function_->code.size());
    }
}

void BytecodeCompiler::CompileFieldInitializers(std::string_view typeName, const AstNode* source) {
    const ClassSignature* type = FindClass(typeName);
    if (type && !type->baseClass.empty()) CompileFieldInitializers(type->baseClass, source);
    const auto found = classNodes_.find(std::string(typeName));
    if (found == classNodes_.end()) return;
    bool hasInitializers = false;
    for (AstNode* member = found->second->firstChild; member; member = member->nextSibling)
        hasInitializers = hasInitializers || (member->kind == NodeKind::FieldDecl && member->firstChild);
    if (!hasInitializers) return;

    Token temporary{TokenKind::Identifier, "$fieldinit", source ? source->token.location : SourceLocation{}};
    const VariableId receiver = DeclareLocal(temporary);
    Emit(OpCode::Dup, 0, source);
    Emit(OpCode::StoreLocal, static_cast<std::int32_t>(receiver.value), source);
    const std::string previousObjectType = currentObjectType_;
    const std::string previousNamespace = currentNamespace_;
    const std::uint32_t previousThisSlot = implicitThisSlot_;
    currentObjectType_ = std::string(typeName);
    currentNamespace_ = NamespaceOf(typeName);
    implicitThisSlot_ = receiver.value;
    std::uint32_t fieldIndex = type ? static_cast<std::uint32_t>(type->inheritedFieldCount) : 0;
    for (AstNode* member = found->second->firstChild; member; member = member->nextSibling) {
        if (member->kind != NodeKind::FieldDecl) continue;
        if (member->firstChild) {
            Emit(OpCode::LoadLocal, static_cast<std::int32_t>(receiver.value), member);
            CompileExpression(member->firstChild);
            EmitConversion(member->firstChild->inferredType, member->declaredType, member);
            Emit(OpCode::StoreField, static_cast<std::int32_t>(fieldIndex), member);
            Emit(OpCode::Pop, 0, member);
        }
        ++fieldIndex;
    }
    currentObjectType_ = previousObjectType;
    currentNamespace_ = previousNamespace;
    implicitThisSlot_ = previousThisSlot;
}

void BytecodeCompiler::CompileImplicitBaseConstructor(std::string_view typeName,
                                                      const AstNode* source) {
    const ClassSignature* type = FindClass(typeName);
    const ClassSignature* base = type && !type->baseClass.empty() ? FindClass(type->baseClass) : nullptr;
    if (!base) return;
    const FunctionSignature* constructor = nullptr;
    for (const auto& candidate : base->methods) {
        if (candidate.constructor && OrderArguments(candidate, {}).has_value()) {
            constructor = &candidate;
            break;
        }
    }
    if (!constructor) {
        CompileImplicitBaseConstructor(base->name, source);
        return;
    }
    Emit(OpCode::LoadLocal, static_cast<std::int32_t>(implicitThisSlot_), source);
    const auto definition = functionNodes_.find(FunctionKey(*constructor));
    AstNode* parameter = definition == functionNodes_.end() ? nullptr : definition->second->firstChild;
    for (std::size_t index = 0; index < constructor->parameters.size(); ++index) {
        while (parameter && parameter->kind != NodeKind::Parameter) parameter = parameter->nextSibling;
        AstNode* expression = parameter ? parameter->firstChild : nullptr;
        if (!expression) { Error(source, "default base constructor argument is missing"); return; }
        const std::string previousNamespace = currentNamespace_;
        currentNamespace_ = NamespaceOf(constructor->objectType);
        ReferenceReceiverMap receivers;
        CompileCallArgument(*constructor, index, expression, receivers);
        currentNamespace_ = previousNamespace;
        parameter = parameter->nextSibling;
    }
    const auto target = functionIds_.find(FunctionKey(*constructor));
    if (target == functionIds_.end()) { Error(source, "base constructor target is missing"); return; }
    Emit(OpCode::Call, AddCallable({CallableKind::ScriptMethod, target->second, base->id, 0,
                                    static_cast<std::uint32_t>(constructor->parameters.size())}), source);
    Emit(OpCode::Pop, 0, source);
}

void BytecodeCompiler::CompileCall(AstNode* node, bool dereferenceResult) {
    AstNode* callee = node->firstChild;
    if (!callee || (callee->kind != NodeKind::Identifier && callee->kind != NodeKind::Member)) {
        Error(node, "callee cannot be compiled"); return;
    }
    const bool explicitMethod = callee->kind == NodeKind::Member;
    const std::string& callName = callee->token.lexeme;
    std::vector<AstNode*> arguments;
    for (AstNode* argument = callee->nextSibling; argument; argument = argument->nextSibling)
        arguments.push_back(argument);
    if (!explicitMethod && callName == "super" && !currentObjectType_.empty()) {
        const ClassSignature* type = FindClass(currentObjectType_);
        const ClassSignature* base = type && !type->baseClass.empty() ? FindClass(type->baseClass) : nullptr;
        if (!base) { Error(node, "base constructor target is missing"); return; }
        const FunctionSignature* constructor = nullptr;
        int bestCost = 1000000;
        for (const auto& candidate : base->methods) {
            if (!candidate.constructor) continue;
            const auto ordered = OrderArguments(candidate, arguments);
            if (!ordered) continue;
            int cost = 0;
            bool viable = true;
            for (std::size_t index = 0; index < ordered->size(); ++index) {
                if (!(*ordered)[index]) continue;
                const auto conversion = ConversionCost(
                    (*ordered)[index]->inferredType, candidate.parameters[index]);
                if (!conversion) { viable = false; break; }
                cost += *conversion;
            }
            if (viable && cost < bestCost) { constructor = &candidate; bestCost = cost; }
        }
        if (!constructor) {
            if (!arguments.empty()) { Error(node, "base constructor target is missing"); return; }
            CompileImplicitBaseConstructor(currentObjectType_, node);
            Emit(OpCode::PushVoid, 0, node);
            return;
        }
        Emit(OpCode::LoadLocal, static_cast<std::int32_t>(implicitThisSlot_), node);
        const auto ordered = OrderArguments(*constructor, arguments);
        if (!ordered) { Error(node, "base constructor arguments cannot be ordered"); return; }
        ReferenceReceiverMap referenceReceivers;
        const auto definition = functionNodes_.find(FunctionKey(*constructor));
        AstNode* parameter = definition == functionNodes_.end() ? nullptr : definition->second->firstChild;
        for (std::size_t index = 0; index < constructor->parameters.size(); ++index) {
            while (parameter && parameter->kind != NodeKind::Parameter) parameter = parameter->nextSibling;
            AstNode* expression = (*ordered)[index] ? (*ordered)[index]
                : (parameter ? parameter->firstChild : nullptr);
            if (!expression) { Error(node, "default base constructor argument is missing"); return; }
            CompileCallArgument(*constructor, index, expression, referenceReceivers);
            if (parameter) parameter = parameter->nextSibling;
        }
        const auto target = functionIds_.find(FunctionKey(*constructor));
        if (target == functionIds_.end()) { Error(node, "base constructor target is missing"); return; }
        Emit(OpCode::Call, AddCallable({CallableKind::ScriptMethod, target->second, base->id, 0,
                                        static_cast<std::uint32_t>(constructor->parameters.size())}), node);
        CompileReferenceWritebacks(*constructor, *ordered, referenceReceivers, node);
        return;
    }
    auto classFound = classIds_.end();
    std::string resolvedClassName;
    for (const auto& candidate : NameCandidates(currentNamespace_, callName)) {
        classFound = classIds_.find(candidate);
        if (classFound != classIds_.end()) { resolvedClassName = candidate; break; }
    }
    if (!explicitMethod && classFound != classIds_.end()) {
        ReferenceReceiverMap referenceReceivers;
        Emit(OpCode::NewObject, static_cast<std::int32_t>(classFound->second.value), node);
        CompileFieldInitializers(resolvedClassName, node);
        const FunctionSignature* constructor = nullptr;
        int bestCost = 1000000;
        for (const auto& signature : signatures_) {
            if (!signature.constructor || signature.objectType != resolvedClassName) continue;
            const auto ordered = OrderArguments(signature, arguments);
            if (!ordered) continue;
            int cost = 0;
            bool viable = true;
            for (std::size_t i = 0; i < ordered->size(); ++i) {
                if (!(*ordered)[i]) continue;
                const ParameterMode mode = ParameterModeAt(signature, i);
                const auto conversion = (mode == ParameterMode::Out || mode == ParameterMode::InOut)
                    ? ((*ordered)[i]->inferredType == signature.parameters[i]
                           ? std::optional<int>{0} : std::nullopt)
                    : ConversionCost((*ordered)[i]->inferredType, signature.parameters[i]);
                if (!conversion) { viable = false; break; }
                cost += *conversion;
            }
            if (viable && cost < bestCost) { constructor = &signature; bestCost = cost; }
        }
        if (constructor) {
            Emit(OpCode::Dup, 0, node);
            const auto ordered = OrderArguments(*constructor, arguments);
            if (!ordered) { Error(node, "constructor arguments cannot be ordered"); return; }
            const auto definition = functionNodes_.find(FunctionKey(*constructor));
            AstNode* parameter = definition == functionNodes_.end() ? nullptr : definition->second->firstChild;
            for (std::size_t i = 0; i < constructor->parameters.size(); ++i) {
                while (parameter && parameter->kind != NodeKind::Parameter) parameter = parameter->nextSibling;
                AstNode* expression = (*ordered)[i] ? (*ordered)[i]
                    : (parameter ? parameter->firstChild : nullptr);
                if (!expression) { Error(node, "default constructor argument is missing"); return; }
                const std::string previousNamespace = currentNamespace_;
                if (!(*ordered)[i]) currentNamespace_ = NamespaceOf(constructor->objectType);
                CompileCallArgument(*constructor, i, expression, referenceReceivers);
                currentNamespace_ = previousNamespace;
                if (parameter) parameter = parameter->nextSibling;
            }
            const auto target = functionIds_.find(FunctionKey(*constructor));
            if (target == functionIds_.end()) { Error(node, "constructor target is missing"); return; }
            Emit(OpCode::Call, AddCallable({CallableKind::ScriptMethod, target->second,
                                            classFound->second, 0,
                                            static_cast<std::uint32_t>(constructor->parameters.size())}), node);
            CompileReferenceWritebacks(*constructor, *ordered, referenceReceivers, node);
            Emit(OpCode::Pop, 0, node);
        } else if (!arguments.empty()) {
            Error(node, "constructor target is missing");
        } else {
            Token temporary{TokenKind::Identifier, "$construction", node->token.location};
            const VariableId receiver = DeclareLocal(temporary);
            Emit(OpCode::Dup, 0, node);
            Emit(OpCode::StoreLocal, static_cast<std::int32_t>(receiver.value), node);
            const std::uint32_t previousThisSlot = implicitThisSlot_;
            implicitThisSlot_ = receiver.value;
            CompileImplicitBaseConstructor(resolvedClassName, node);
            implicitThisSlot_ = previousThisSlot;
        }
        return;
    }
    const FunctionSignature* target = nullptr;
    int bestCost = 1000000;
    const std::string receiverType = explicitMethod
        ? callee->firstChild->inferredType.objectName : currentObjectType_;
    std::string requestedName = callName;
    std::string lookupType = receiverType;
    if (node->nonVirtualCall) {
        const auto separator = callName.rfind("::");
        if (separator != std::string::npos) {
            lookupType = callName.substr(0, separator);
            requestedName = callName.substr(separator + 2);
        }
    }
    for (const auto& signature : signatures_) {
        if (signature.constructor || signature.destructor) continue;
        const auto ordered = OrderArguments(signature, arguments);
        if (!ordered) continue;
        std::optional<int> nameCost;
        if (signature.method) {
            if (signature.name == requestedName && IsBaseOf(signature.objectType, lookupType)) {
                int distance = 0;
                const ClassSignature* owner = FindClass(lookupType);
                while (owner && owner->name != signature.objectType) {
                    ++distance;
                    owner = owner->baseClass.empty() ? nullptr : FindClass(owner->baseClass);
                }
                nameCost = distance;
            }
        } else if (!node->nonVirtualCall) {
            nameCost = NameMatchCost(signature.name, callName, currentNamespace_);
        }
        if (!nameCost) continue;
        if (explicitMethod && !signature.method) continue;
        if (!explicitMethod && signature.method && currentObjectType_.empty()) continue;
        if (!explicitMethod && currentObjectType_.empty() && signature.method) continue;
        int cost = *nameCost +
            ((!explicitMethod && !currentObjectType_.empty() && !signature.method) ? 1000 : 0);
        bool viable = true;
        for (std::size_t i = 0; i < ordered->size(); ++i) {
            if (!(*ordered)[i]) continue;
            const ParameterMode mode = ParameterModeAt(signature, i);
            const auto conversion = (mode == ParameterMode::Out || mode == ParameterMode::InOut)
                ? ((*ordered)[i]->inferredType == signature.parameters[i]
                       ? std::optional<int>{0} : std::nullopt)
                : ConversionCost((*ordered)[i]->inferredType, signature.parameters[i]);
            if (!conversion) { viable = false; break; }
            cost += *conversion;
        }
        if (viable && cost < bestCost) { target = &signature; bestCost = cost; }
    }
    if (!target) { Error(node, "cannot resolve script call"); return; }
    const auto ordered = OrderArguments(*target, arguments);
    if (!ordered) { Error(node, "call arguments cannot be ordered"); return; }
    ReferenceReceiverMap referenceReceivers;
    if (target->method) {
        if (explicitMethod) CompileExpression(callee->firstChild);
        else Emit(OpCode::LoadLocal, static_cast<std::int32_t>(implicitThisSlot_), callee);
    }
    const auto definition = functionNodes_.find(FunctionKey(*target));
    AstNode* parameter = definition == functionNodes_.end() ? nullptr : definition->second->firstChild;
    for (std::size_t i = 0; i < target->parameters.size(); ++i) {
        while (parameter && parameter->kind != NodeKind::Parameter) parameter = parameter->nextSibling;
        AstNode* expression = (*ordered)[i] ? (*ordered)[i]
            : (parameter ? parameter->firstChild : nullptr);
        if (!expression) { Error(node, "default argument is missing"); return; }
        const std::string previousNamespace = currentNamespace_;
        if (!(*ordered)[i]) currentNamespace_ = NamespaceOf(
            target->method ? target->objectType : target->name);
        CompileCallArgument(*target, i, expression, referenceReceivers);
        currentNamespace_ = previousNamespace;
        if (parameter) parameter = parameter->nextSibling;
    }
    if (target->method) {
        const ClassSignature* ownerType = nullptr;
        for (const auto& type : classes_) if (type.name == target->objectType) ownerType = &type;
        const ClassSignature* staticType = FindClass(receiverType);
        if (ownerType && ownerType->interfaceType) {
            std::uint32_t slot = 0;
            bool foundSlot = false;
            for (std::size_t index = 0; index < ownerType->methods.size(); ++index) {
                const auto& method = ownerType->methods[index];
                if (method.name == target->name && method.returnType == target->returnType &&
                    method.parameters == target->parameters &&
                    method.parameterModes == target->parameterModes &&
                    method.returnsReference == target->returnsReference &&
                    method.returnReferenceConst == target->returnReferenceConst) {
                    slot = static_cast<std::uint32_t>(index);
                    foundSlot = true;
                    break;
                }
            }
            if (!foundSlot) { Error(node, "virtual method slot is missing"); return; }
            Emit(OpCode::CallVirtual,
                 AddCallable({CallableKind::VirtualMethod, {}, ownerType->id, slot,
                              static_cast<std::uint32_t>(target->parameters.size())}), node);
            CompileReferenceWritebacks(*target, *ordered, referenceReceivers, node);
            if (target->returnsReference && dereferenceResult) Emit(OpCode::LoadReference, 0, node);
            return;
        }
        if (staticType && !node->nonVirtualCall) {
            const auto layout = VirtualLayout(*staticType);
            std::uint32_t slot = 0;
            bool foundSlot = false;
            for (std::size_t index = 0; index < layout.size(); ++index) {
                const auto* method = layout[index];
                if (method->name == target->name && method->parameters == target->parameters &&
                    method->parameterModes == target->parameterModes) {
                    slot = static_cast<std::uint32_t>(index);
                    foundSlot = true;
                    break;
                }
            }
            if (!foundSlot) { Error(node, "class virtual method slot is missing"); return; }
            Emit(OpCode::CallVirtual,
                 AddCallable({CallableKind::VirtualMethod, {}, staticType->id, slot,
                              static_cast<std::uint32_t>(target->parameters.size())}), node);
            CompileReferenceWritebacks(*target, *ordered, referenceReceivers, node);
            if (target->returnsReference && dereferenceResult) Emit(OpCode::LoadReference, 0, node);
            return;
        }
    }
    if (target->host) {
        const auto found = hostIds_.find(FunctionKey(*target));
        if (found == hostIds_.end()) { Error(node, "host call target is missing"); return; }
        Emit(OpCode::CallHost,
             AddCallable({CallableKind::HostFunction, found->second, {}, 0,
                          static_cast<std::uint32_t>(target->parameters.size())}), node);
    } else {
        const auto found = functionIds_.find(FunctionKey(*target));
        if (found == functionIds_.end()) { Error(node, "script call target is missing"); return; }
        TypeId owner;
        if (target->method) {
            for (const auto& type : classes_) if (type.name == target->objectType) owner = type.id;
        }
        Emit(OpCode::Call, AddCallable({target->method ? CallableKind::ScriptMethod
                                                      : CallableKind::ScriptFunction,
                                       found->second, owner, 0,
                                       static_cast<std::uint32_t>(target->parameters.size())}), node);
    }
    CompileReferenceWritebacks(*target, *ordered, referenceReceivers, node);
    if (target->returnsReference && dereferenceResult) Emit(OpCode::LoadReference, 0, node);
}

void BytecodeCompiler::CompileReferenceTarget(AstNode* expression, const AstNode* source) {
    if (expression && expression->kind == NodeKind::Call && expression->returnsReference) {
        CompileCall(expression, false);
        return;
    }
    const auto target = ResolveLValue(expression);
    if (!target) { Error(source, "return reference target cannot be compiled"); return; }
    if (target->kind == LValueRef::Kind::Global) {
        Emit(OpCode::MakeGlobalReference, static_cast<std::int32_t>(target->global.value), source);
    } else if (target->kind == LValueRef::Kind::Field) {
        if (target->receiver) CompileExpression(target->receiver);
        else Emit(OpCode::LoadLocal, static_cast<std::int32_t>(implicitThisSlot_), source);
        Emit(OpCode::MakeFieldReference, static_cast<std::int32_t>(target->field), source);
    } else Error(source, "cannot return reference to local storage");
}

void BytecodeCompiler::CompileCallArgument(const FunctionSignature& signature, std::size_t index,
                                           AstNode* expression,
                                           ReferenceReceiverMap& receivers) {
    const ParameterMode mode = ParameterModeAt(signature, index);
    const auto target = (mode == ParameterMode::Out || mode == ParameterMode::InOut)
        ? ResolveLValue(expression) : std::optional<LValueRef>{};
    if ((mode == ParameterMode::Out || mode == ParameterMode::InOut) && !target) {
        Error(expression, "reference argument cannot be compiled as an lvalue");
        EmitDefaultValue(signature.parameters[index], expression);
        return;
    }
    if (target && target->kind == LValueRef::Kind::Field && target->receiver) {
        CompileExpression(target->receiver);
        Emit(OpCode::Dup, 0, expression);
        const VariableId receiverSlot{nextLocal_++};
        Emit(OpCode::StoreLocal, static_cast<std::int32_t>(receiverSlot.value), expression);
        receivers.emplace(expression, receiverSlot);
        if (mode == ParameterMode::Out) Emit(OpCode::Pop, 0, expression);
        else Emit(OpCode::LoadField, static_cast<std::int32_t>(target->field), expression);
        if (mode == ParameterMode::Out)
            EmitDefaultValue(signature.parameters[index], expression);
        return;
    }
    if (mode == ParameterMode::Out) {
        EmitDefaultValue(signature.parameters[index], expression);
        return;
    }
    if (mode == ParameterMode::InOut) {
        CompileLValueLoad(*target, expression);
        return;
    }
    CompileExpression(expression);
    EmitConversion(expression->inferredType, signature.parameters[index], expression);
}

void BytecodeCompiler::CompileReferenceWritebacks(
    const FunctionSignature& signature, const std::vector<AstNode*>& arguments,
    const ReferenceReceiverMap& receivers, const AstNode* source) {
    for (std::size_t index = signature.parameters.size(); index > 0; --index) {
        const std::size_t parameter = index - 1;
        const ParameterMode mode = ParameterModeAt(signature, parameter);
        if (mode != ParameterMode::Out && mode != ParameterMode::InOut) continue;
        AstNode* expression = parameter < arguments.size() ? arguments[parameter] : nullptr;
        const auto target = ResolveLValue(expression);
        if (!target) {
            Error(expression ? expression : source, "reference argument cannot be written back");
            Emit(OpCode::Pop, 0, source);
            continue;
        }
        switch (target->kind) {
        case LValueRef::Kind::Local:
            Emit(OpCode::StoreLocal, static_cast<std::int32_t>(target->variable.value), source);
            break;
        case LValueRef::Kind::Global:
            Emit(OpCode::StoreGlobal, static_cast<std::int32_t>(target->global.value), source);
            break;
        case LValueRef::Kind::Field:
            if (target->receiver) {
                const auto receiver = receivers.find(expression);
                if (receiver == receivers.end()) {
                    Error(expression, "reference receiver snapshot is missing");
                    Emit(OpCode::Pop, 0, source);
                    break;
                }
                Emit(OpCode::LoadLocal, static_cast<std::int32_t>(receiver->second.value), source);
            }
            else Emit(OpCode::LoadLocal, static_cast<std::int32_t>(implicitThisSlot_), source);
            Emit(OpCode::Swap, 0, source);
            Emit(OpCode::StoreField, static_cast<std::int32_t>(target->field), source);
            Emit(OpCode::Pop, 0, source);
            break;
        case LValueRef::Kind::Dynamic:
            Error(source, "dynamic reference argument writeback is not implemented");
            Emit(OpCode::Pop, 0, source);
            break;
        case LValueRef::Kind::Index:
            Error(source, "reference index writeback is not implemented");
            Emit(OpCode::Pop, 0, source);
            break;
        }
    }
}

void BytecodeCompiler::EmitDefaultValue(const DataType& type, const AstNode* source) {
    if (type == DataType::Bool()) Emit(OpCode::PushConst, AddConstant(Value(false)), source);
    else if (type.IsInteger())
        Emit(OpCode::PushConst, AddConstant(Value::Integer(type, 0)), source);
    else if (type == DataType::Float()) Emit(OpCode::PushConst, AddConstant(Value(0.0f)), source);
    else if (type == DataType::Double()) Emit(OpCode::PushConst, AddConstant(Value(0.0)), source);
    else if (type == DataType::String()) Emit(OpCode::PushConst, AddConstant(Value("")), source);
    else if (type.kind == TypeKind::Object)
        Emit(OpCode::PushConst, AddConstant(Value(ObjectHandle{})), source);
    else Emit(OpCode::PushVoid, 0, source);
}

void BytecodeCompiler::EmitConversion(const DataType& from, const DataType& to,
                                      const AstNode* source) {
    if (from == to) return;
    if (from.IsInteger() && to.IsInteger()) {
        Emit(OpCode::ToInteger, static_cast<std::int32_t>(to.kind), source);
    } else if (from.IsInteger() && to == DataType::Float()) {
        Emit(OpCode::ToFloat, 0, source);
    } else if ((from.IsInteger() || from == DataType::Float()) && to == DataType::Double()) {
        Emit(OpCode::ToDouble, 0, source);
    } else if (from == DataType::Double() && to == DataType::Float()) {
        Emit(OpCode::ToFloat, 0, source);
    }
}

std::optional<int> BytecodeCompiler::ConversionCost(const DataType& from,
                                                     const DataType& to) const {
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
    if (from.kind == TypeKind::Object && from.objectName == "<null>" && to.isHandle) return 1;
    if (from.kind == TypeKind::Object && to.kind == TypeKind::Object &&
        from.isHandle && to.isHandle) {
        if (IsBaseOf(to.objectName, from.objectName)) return 1;
        const ClassSignature* type = FindClass(from.objectName);
        if (type && std::find(type->interfaces.begin(), type->interfaces.end(), to.objectName) !=
                    type->interfaces.end()) return 1;
    }
    return std::nullopt;
}

std::int32_t BytecodeCompiler::AddCallable(CallableRef callable) {
    for (std::size_t i = 0; i < module_.callables.size(); ++i) {
        const auto& existing = module_.callables[i];
        if (existing.kind == callable.kind && existing.function == callable.function &&
            existing.objectType == callable.objectType && existing.virtualSlot == callable.virtualSlot &&
            existing.parameterCount == callable.parameterCount)
            return static_cast<std::int32_t>(i);
    }
    module_.callables.push_back(std::move(callable));
    return static_cast<std::int32_t>(module_.callables.size() - 1);
}

std::optional<std::pair<std::size_t, DataType>> BytecodeCompiler::FindField(const AstNode* member) const {
    if (!member || !member->firstChild) return std::nullopt;
    const std::string& typeName = member->firstChild->inferredType.objectName;
    for (const auto& type : classes_) {
        if (type.name != typeName) continue;
        for (std::size_t i = 0; i < type.fields.size(); ++i) {
            if (type.fields[i].name == member->token.lexeme)
                return std::make_pair(i, type.fields[i].type);
        }
    }
    return std::nullopt;
}

const ClassSignature* BytecodeCompiler::FindClass(std::string_view name) const {
    for (const auto& type : classes_) if (type.name == name) return &type;
    return nullptr;
}

bool BytecodeCompiler::IsBaseOf(std::string_view base, std::string_view derived) const {
    const ClassSignature* type = FindClass(derived);
    while (type) {
        if (type->name == base) return true;
        type = type->baseClass.empty() ? nullptr : FindClass(type->baseClass);
    }
    return false;
}

const FunctionSignature* BytecodeCompiler::FindClassMethod(
    const ClassSignature& type, const FunctionSignature& signature) const {
    for (const auto& method : type.methods) {
        if (method.constructor || method.destructor) continue;
        if (method.name == signature.name && method.returnType == signature.returnType &&
            method.parameters == signature.parameters &&
            method.parameterModes == signature.parameterModes &&
            method.returnsReference == signature.returnsReference &&
            method.returnReferenceConst == signature.returnReferenceConst) return &method;
    }
    const ClassSignature* base = type.baseClass.empty() ? nullptr : FindClass(type.baseClass);
    return base ? FindClassMethod(*base, signature) : nullptr;
}

std::vector<const FunctionSignature*> BytecodeCompiler::VirtualLayout(
    const ClassSignature& type) const {
    std::vector<const FunctionSignature*> layout;
    if (const ClassSignature* base = type.baseClass.empty() ? nullptr : FindClass(type.baseClass))
        layout = VirtualLayout(*base);
    for (const auto& method : type.methods) {
        if (method.constructor || method.destructor) continue;
        auto overridden = std::find_if(layout.begin(), layout.end(), [&](const auto* baseMethod) {
            return baseMethod->name == method.name && baseMethod->parameters == method.parameters &&
                   baseMethod->parameterModes == method.parameterModes;
        });
        if (overridden == layout.end()) layout.push_back(&method);
        else *overridden = &method;
    }
    return layout;
}

std::size_t BytecodeCompiler::Emit(OpCode opcode, std::int32_t operand, const AstNode* node) {
    function_->code.push_back({opcode, operand, node ? node->token.location : SourceLocation{}});
    return function_->code.size() - 1;
}

void BytecodeCompiler::PatchJump(std::size_t instruction, std::size_t target) {
    if (instruction >= function_->code.size()) return;
    function_->code[instruction].operand = static_cast<std::int32_t>(target);
}

std::int32_t BytecodeCompiler::AddConstant(Value value) {
    function_->constants.push_back(std::move(value));
    return static_cast<std::int32_t>(function_->constants.size() - 1);
}

std::optional<VariableId> BytecodeCompiler::LookupLocal(std::string_view name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(std::string(name));
        if (found != scope->end()) return found->second;
    }
    return std::nullopt;
}

VariableId BytecodeCompiler::DeclareLocal(const Token& name) {
    const VariableId slot{nextLocal_++};
    scopes_.back()[name.lexeme] = slot;
    return slot;
}

void BytecodeCompiler::Error(const AstNode* node, std::string message) {
    diagnostics_.Report(node ? node->token.location : SourceLocation{}, Severity::Error, std::move(message));
}

} // namespace mini_as
