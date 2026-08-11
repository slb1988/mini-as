#include "mini_as/engine.hpp"
#include "bytecode_io.hpp"

#include <utility>
#include <algorithm>
#include <unordered_map>
#include <cctype>

namespace mini_as {
namespace {

std::string NamespaceOf(std::string_view qualifiedName) {
    const auto separator = qualifiedName.rfind("::");
    return separator == std::string_view::npos ? std::string{}
                                               : std::string(qualifiedName.substr(0, separator));
}

Value DefaultGlobalValue(const DataType& type, const ScriptEngine& engine) {
    if (type == DataType::Bool()) return Value(false);
    if (type.IsInteger()) return Value::Integer(type, 0);
    if (type == DataType::Float()) return Value(0.0f);
    if (type == DataType::Double()) return Value(0.0);
    if (type == DataType::String()) return Value(std::string{});
    if (type.kind == TypeKind::Object) {
        const TypeInfo* registered = engine.GetTypeInfo(type.objectName);
        if (registered && registered->valueType) return registered->defaultValue;
        return Value(ObjectHandle{});
    }
    if (type.kind == TypeKind::Function) return Value(FunctionHandle{{}, {}, type.objectName, false});
    if (type.kind == TypeKind::WeakRef || type.kind == TypeKind::ConstWeakRef)
        return Value(WeakObjectHandle(type.objectName, type.kind == TypeKind::ConstWeakRef));
    return Value{};
}

std::string QualifyName(std::string_view nameSpace, std::string name) {
    if (nameSpace.empty() || name.find("::") != std::string::npos) return name;
    return std::string(nameSpace) + "::" + name;
}

bool IsValidNamespace(std::string_view nameSpace) {
    if (nameSpace.empty()) return true;
    std::size_t begin = 0;
    while (begin < nameSpace.size()) {
        const std::size_t end = nameSpace.find("::", begin);
        const auto part = nameSpace.substr(begin,
            end == std::string_view::npos ? nameSpace.size() - begin : end - begin);
        if (part.empty() || (!std::isalpha(static_cast<unsigned char>(part.front())) &&
                             part.front() != '_')) return false;
        for (const char character : part)
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_')
                return false;
        if (end == std::string_view::npos) return true;
        begin = end + 2;
    }
    return false;
}

bool IsVisible(std::uint32_t registrationMask, std::uint32_t moduleMask) {
    return (registrationMask & moduleMask) != 0;
}

std::optional<std::pair<std::string, std::vector<std::string>>>
ParseTemplateTypeDeclaration(std::string_view declaration, DiagnosticSink& diagnostics) {
    Tokenizer tokenizer("registration", declaration, diagnostics);
    const auto tokens = tokenizer.ScanAll();
    std::size_t index = 0;
    if (tokens.empty() || tokens[index].kind != TokenKind::Identifier) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "template type declaration must start with a name");
        return std::nullopt;
    }
    std::string name = tokens[index++].lexeme;
    while (index + 1 < tokens.size() && tokens[index].kind == TokenKind::Scope &&
           tokens[index + 1].kind == TokenKind::Identifier) {
        name += "::" + tokens[index + 1].lexeme;
        index += 2;
    }
    if (index >= tokens.size() || tokens[index++].kind != TokenKind::Less) {
        diagnostics.Report(tokens[std::min(index, tokens.size() - 1)].location,
                           Severity::Error,
                           "template type declaration requires '<class T>'");
        return std::nullopt;
    }
    std::vector<std::string> parameters;
    while (index < tokens.size() && tokens[index].kind != TokenKind::Greater) {
        if (tokens[index].kind != TokenKind::KwClass) {
            diagnostics.Report(tokens[index].location, Severity::Error,
                               "template subtype parameter requires the 'class' keyword");
            return std::nullopt;
        }
        ++index;
        if (index >= tokens.size() || tokens[index].kind != TokenKind::Identifier) {
            diagnostics.Report(tokens[std::min(index, tokens.size() - 1)].location,
                               Severity::Error, "expected template subtype parameter name");
            return std::nullopt;
        }
        const std::string parameter = tokens[index++].lexeme;
        if (std::find(parameters.begin(), parameters.end(), parameter) != parameters.end()) {
            diagnostics.Report(tokens[index - 1].location, Severity::Error,
                               "duplicate template subtype parameter '" + parameter + "'");
            return std::nullopt;
        }
        parameters.push_back(parameter);
        if (index < tokens.size() && tokens[index].kind == TokenKind::Comma) ++index;
        else break;
    }
    if (parameters.empty() || index >= tokens.size() ||
        tokens[index++].kind != TokenKind::Greater ||
        index >= tokens.size() || tokens[index].kind != TokenKind::End) {
        diagnostics.Report(tokens[std::min(index, tokens.size() - 1)].location,
                           Severity::Error, "invalid template type declaration");
        return std::nullopt;
    }
    return std::make_pair(std::move(name), std::move(parameters));
}

void CollectDynamicDeclarations(AstNode* node, std::vector<AstNode*>& declarations) {
    if (!node) return;
    if (node->kind == NodeKind::Program || node->kind == NodeKind::NamespaceDecl) {
        for (AstNode* child = node->firstChild; child; child = child->nextSibling)
            CollectDynamicDeclarations(child, declarations);
        return;
    }
    declarations.push_back(node);
}

bool CallsFunctionDirectly(AstNode* node, std::string_view qualifiedName) {
    if (!node) return false;
    const auto separator = qualifiedName.rfind("::");
    const std::string_view simpleName = separator == std::string_view::npos
        ? qualifiedName : qualifiedName.substr(separator + 2);
    if (node->kind == NodeKind::Call && node->firstChild &&
        node->firstChild->kind == NodeKind::Identifier &&
        (node->firstChild->token.lexeme == qualifiedName ||
         node->firstChild->token.lexeme == simpleName)) return true;
    for (AstNode* child = node->firstChild; child; child = child->nextSibling)
        if (CallsFunctionDirectly(child, qualifiedName)) return true;
    return false;
}

bool UsesCallableDescriptor(OpCode opcode) {
    return opcode == OpCode::Call || opcode == OpCode::CallHost ||
           opcode == OpCode::CallVirtual || opcode == OpCode::CallHandle ||
           opcode == OpCode::MakeDelegate || opcode == OpCode::MakeClosure;
}

} // namespace

std::string_view Version() { return "0.1.0-learning"; }

ScriptModule::ScriptModule(ScriptEngine& engine, std::string name)
    : engine_(engine), name_(std::move(name)), image_(std::make_shared<ModuleImage>()),
      accessMask_(engine.GetDefaultAccessMask()) {}

const std::string& ScriptModule::GetName() const { return name_; }

void ScriptModule::AddScriptSection(std::string name, std::string source) {
    sections_.push_back({std::move(name), std::move(source)});
}

bool ScriptModule::Build() {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { engine_.ForwardDiagnostic(diagnostic); });
    std::vector<Token> tokens;
    for (const auto& section : sections_) {
        Tokenizer tokenizer(section.name, section.source, diagnostics);
        auto sectionTokens = tokenizer.ScanAll();
        if (!sectionTokens.empty()) sectionTokens.pop_back();
        tokens.insert(tokens.end(), std::make_move_iterator(sectionTokens.begin()),
                      std::make_move_iterator(sectionTokens.end()));
    }
    SourceLocation endLocation{sections_.empty() ? name_ : sections_.back().name};
    tokens.push_back({TokenKind::End, {}, std::move(endLocation)});
    Parser parser(std::move(tokens), diagnostics);
    const auto hostEnums = engine_.HostEnums(accessMask_);
    const auto hostTypedefs = engine_.HostTypedefs(accessMask_);
    const auto hostFuncdefs = engine_.HostFuncdefs(accessMask_);
    const auto hostTemplates = engine_.HostTemplateTypes(accessMask_);
    for (const auto& type : hostEnums) parser.RegisterEnumType(type.name);
    for (const auto& type : hostTypedefs)
        parser.RegisterTypedefType(type.name, type.underlyingType);
    for (const auto& type : hostFuncdefs) parser.RegisterFuncdefType(type.name);
    for (const auto& type : hostTemplates) parser.RegisterTemplateType(type.first, type.second);
    auto tree = parser.Parse();
    if (!diagnostics.HasErrors() &&
        !engine_.InstantiateTemplateTypes(parser.TemplateTypeUses(), accessMask_, diagnostics))
        return false;
    TypeChecker checker(diagnostics);
    for (const auto& signature : engine_.HostSignatures(accessMask_)) checker.RegisterFunction(signature);
    for (const auto& signature : engine_.HostPropertySignatures(accessMask_))
        checker.RegisterGlobalProperty(signature);
    for (const auto& signature : engine_.HostTypeSignatures(accessMask_))
        checker.RegisterObjectType(signature);
    for (const auto& signature : hostEnums) checker.RegisterEnum(signature);
    for (const auto& signature : hostTypedefs) checker.RegisterTypedef(signature);
    for (const auto& signature : hostFuncdefs) checker.RegisterFuncdef(signature);
    const bool typed = !diagnostics.HasErrors() && checker.Check(tree.root);
    if (!typed) return false;
    auto functions = checker.Functions();
    for (auto& function : functions) {
        if (!function.id.IsValid()) {
            function.id = engine_.GetOrCreateFunctionId(name_ + "\n" + function.Declaration());
        }
    }
    auto classes = checker.Classes();
    for (auto& type : classes) {
        if (!type.id.IsValid()) type.id = engine_.GetOrCreateTypeId(type.name);
        for (auto& method : type.methods) {
            method.objectType = type.name;
            method.method = true;
            if (!method.id.IsValid())
                method.id = engine_.GetOrCreateFunctionId(
                    name_ + "\n" + type.name + "::" + method.Declaration());
        }
    }
    auto globals = checker.Globals();
    for (auto& global : globals) {
        if (!global.id.IsValid())
            global.id = engine_.GetOrCreateGlobalId(name_ + "\n" + global.name);
    }
    BytecodeCompiler compiler(diagnostics);
    auto enums = checker.Enums();
    for (auto& type : enums) type.id = engine_.GetOrCreateTypeId(type.name);
    auto typedefs = checker.Typedefs();
    for (auto& type : typedefs) type.id = engine_.GetOrCreateTypeId(type.name);
    auto funcdefs = checker.Funcdefs();
    for (auto& type : funcdefs) type.id = engine_.GetOrCreateTypeId(type.name);
    BytecodeModule candidate = compiler.Compile(tree.root, functions, classes, globals, enums, funcdefs);
    if (diagnostics.HasErrors()) return false;
    std::vector<const TypeInfo*> linkedTypes;
    for (const auto& type : classes) {
        const TypeInfo* linked = type.host ? engine_.GetTypeInfo(type.name)
                                          : engine_.RegisterScriptType(type);
        if (linked) linkedTypes.push_back(linked);
    }
    for (const auto& type : classes) if (!type.host) engine_.LinkScriptType(type);
    for (const auto& host : engine_.hostFunctions_)
        if (host.active && IsVisible(host.accessMask, accessMask_))
            candidate.hostFunctions.push_back({host.signature.id, &host});
    for (auto& binding : candidate.globals) {
        if (!binding.signature.host) continue;
        for (const auto& host : engine_.hostProperties_) {
            if (!host.active || !IsVisible(host.accessMask, accessMask_)) continue;
            if (host.signature.id == binding.signature.id) {
                binding.host = &host;
                break;
            }
        }
        if (!binding.host) {
            diagnostics.Report({"registration"}, Severity::Error,
                               "registered global property binding is unavailable");
            return false;
        }
    }
    for (const auto* type : linkedTypes) candidate.objectTypes.push_back({type->id, type});
    auto state = std::make_shared<ModuleState>();
    state->globals.reserve(candidate.globals.size());
    for (const auto& global : candidate.globals) {
        state->globals.push_back(global.host && global.host->storage
            ? *global.host->storage : DefaultGlobalValue(global.signature.type, engine_));
    }
    VirtualMachine initializer;
    auto finalizerModule = std::make_shared<BytecodeModule>(candidate);
    initializer.SetFinalizerContext(&engine_, finalizerModule, state,
                                    [this] { engine_.DrainFinalizers(); });
    const auto initialized = initializer.Execute(candidate.globalInitializer, {}, &candidate, state.get());
    engine_.DrainFinalizers();
    if (initialized.state != ExecutionState::Finished) {
        diagnostics.Report(initialized.location, Severity::Error,
                           "global initialization failed: " + initialized.exception);
        return false;
    }
    for (const auto& type : classes)
        if (!type.host) engine_.PublishObjectMetadata(type);
    for (const auto& type : enums) {
        bool host = false;
        for (const auto& registered : engine_.hostEnums_)
            host = host || registered.name == type.name;
        if (!host) engine_.PublishEnumMetadata(type, false);
    }
    for (const auto& type : typedefs) {
        bool host = false;
        for (const auto& registered : engine_.hostTypedefs_)
            host = host || registered.name == type.name;
        if (!host) engine_.PublishTypedefMetadata(type, false);
    }
    for (const auto& type : funcdefs) {
        bool host = false;
        for (const auto& registered : engine_.hostFuncdefs_)
            host = host || registered.name == type.name;
        if (!host) engine_.PublishFuncdefMetadata(type, false);
    }
    for (const auto& function : functions)
        if (!function.host) engine_.PublishFunctionMetadata(function, name_);
    for (const auto& type : classes)
        if (!type.host)
            for (const auto& method : type.methods)
                engine_.PublishFunctionMetadata(method, name_);
    for (const auto& global : globals)
        if (!global.host) engine_.PublishGlobalMetadata(global, name_);
    auto nextImage = std::make_shared<ModuleImage>();
    nextImage->bytecode = std::move(candidate);
    nextImage->finalizerBytecode = std::move(finalizerModule);
    nextImage->state = std::move(state);
    nextImage->environment.functions = std::move(functions);
    nextImage->environment.classes = std::move(classes);
    nextImage->environment.globals = std::move(globals);
    nextImage->environment.enums = std::move(enums);
    nextImage->environment.typedefs = std::move(typedefs);
    nextImage->environment.funcdefs = std::move(funcdefs);
    nextImage->definitionTrees.push_back(
        std::make_shared<SyntaxTree>(std::move(tree)));
    engine_.RegisterModuleImage(nextImage);
    image_ = std::move(nextImage);
    sections_.clear();
    return true;
}

const BytecodeFunction* ScriptModule::GetFunctionByDecl(std::string_view declaration) const {
    std::string requested(declaration);
    if (!defaultNamespace_.empty()) {
        DiagnosticSink diagnostics;
        auto parsed = ParseFunctionDeclaration(declaration, diagnostics);
        if (parsed && parsed->name.find("::") == std::string::npos) {
            parsed->name = QualifyName(defaultNamespace_, parsed->name);
            requested = parsed->Declaration();
        }
    }
    for (const auto& function : image_->bytecode.functions) {
        if (std::find(image_->removedFunctions.begin(), image_->removedFunctions.end(),
                      function.signature.id) != image_->removedFunctions.end()) continue;
        if (function.signature.Declaration() == requested) return &function;
    }
    return nullptr;
}

const BytecodeFunction* ScriptModule::GetFunctionByName(std::string_view name) const {
    const std::string requested = QualifyName(defaultNamespace_, std::string(name));
    for (const auto& function : image_->bytecode.functions) {
        if (std::find(image_->removedFunctions.begin(), image_->removedFunctions.end(),
                      function.signature.id) != image_->removedFunctions.end()) continue;
        if (function.signature.name == requested) return &function;
    }
    return nullptr;
}

const FunctionMetadata* ScriptModule::GetFunctionMetadataByDecl(
    std::string_view declaration) const {
    const BytecodeFunction* function = GetFunctionByDecl(declaration);
    return function ? engine_.GetFunctionMetadataById(function->signature.id) : nullptr;
}

std::size_t ScriptModule::GetGlobalMetadataCount() const {
    return static_cast<std::size_t>(std::count_if(
        image_->bytecode.globals.begin(), image_->bytecode.globals.end(),
        [](const GlobalBinding& binding) { return !binding.signature.host; }));
}

const GlobalMetadata* ScriptModule::GetGlobalMetadataByIndex(std::size_t index) const {
    for (const auto& binding : image_->bytecode.globals) {
        if (binding.signature.host) continue;
        if (index-- == 0) return engine_.FindGlobalMetadata(binding.signature.id);
    }
    return nullptr;
}

const BytecodeFunction* ScriptModule::CompileFunction(std::string sectionName,
                                                      std::string source,
                                                      bool addToModule,
                                                      int lineOffset) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) {
        engine_.ForwardDiagnostic(diagnostic);
    });
    if (!defaultNamespace_.empty())
        source = "namespace " + defaultNamespace_ + " { " + source + " }";
    Tokenizer tokenizer(sectionName, source, diagnostics);
    auto tokens = tokenizer.ScanAll();
    for (auto& token : tokens) token.location.row += lineOffset;
    Parser parser(std::move(tokens), diagnostics);

    ModuleCompilationEnvironment base = image_->environment;
    if (!image_->state) {
        base.functions = engine_.HostSignatures(accessMask_);
        base.globals = engine_.HostPropertySignatures(accessMask_);
        base.classes = engine_.HostTypeSignatures(accessMask_);
        base.enums = engine_.HostEnums(accessMask_);
        base.typedefs = engine_.HostTypedefs(accessMask_);
        base.funcdefs = engine_.HostFuncdefs(accessMask_);
    }
    for (const auto& type : base.enums) parser.RegisterEnumType(type.name);
    for (const auto& type : base.typedefs)
        parser.RegisterTypedefType(type.name, type.underlyingType);
    for (const auto& type : base.funcdefs) parser.RegisterFuncdefType(type.name);
    for (const auto& type : engine_.HostTemplateTypes(accessMask_))
        parser.RegisterTemplateType(type.first, type.second);
    auto tree = parser.Parse();
    if (!diagnostics.HasErrors() &&
        !engine_.InstantiateTemplateTypes(parser.TemplateTypeUses(), accessMask_, diagnostics))
        return nullptr;
    for (const auto& type : engine_.HostTypeSignatures(accessMask_)) {
        const bool known = std::any_of(base.classes.begin(), base.classes.end(),
            [&](const ClassSignature& existing) { return existing.name == type.name; });
        if (!known) base.classes.push_back(type);
    }

    std::vector<AstNode*> declarations;
    CollectDynamicDeclarations(tree.root, declarations);
    if (declarations.size() != 1 || declarations.front()->kind != NodeKind::FunctionDecl) {
        diagnostics.Report({sectionName}, Severity::Error,
                           "dynamic code must contain exactly one function");
        return nullptr;
    }
    AstNode* declaration = declarations.front();
    if (!addToModule && CallsFunctionDirectly(declaration, declaration->token.lexeme)) {
        diagnostics.Report(declaration->token.location, Severity::Error,
                           "detached dynamic function cannot call itself");
        return nullptr;
    }

    TypeChecker checker(diagnostics);
    for (const auto& signature : base.functions) checker.RegisterFunction(signature);
    for (const auto& signature : base.globals) checker.RegisterGlobalProperty(signature);
    for (const auto& signature : base.classes) checker.RegisterObjectType(signature);
    for (const auto& signature : base.enums) checker.RegisterEnum(signature);
    for (const auto& signature : base.typedefs) checker.RegisterTypedef(signature);
    for (const auto& signature : base.funcdefs) checker.RegisterFuncdef(signature);
    if (diagnostics.HasErrors() || !checker.Check(tree.root)) return nullptr;

    auto functions = checker.Functions();
    FunctionId primaryId;
    std::vector<FunctionId> dynamicIds;
    const std::uint64_t serial = nextDynamicFunctionSerial_++;
    for (auto& function : functions) {
        if (function.id.IsValid()) continue;
        function.id = engine_.GetOrCreateFunctionId(
            name_ + "\n$dynamic:" + std::to_string(serial) + "\n" +
            function.Declaration());
        if (!primaryId.IsValid()) primaryId = function.id;
        dynamicIds.push_back(function.id);
    }
    if (!primaryId.IsValid()) {
        diagnostics.Report(declaration->token.location, Severity::Error,
                           "dynamic function did not produce a callable signature");
        return nullptr;
    }

    auto classes = checker.Classes();
    auto globals = checker.Globals();
    auto enums = checker.Enums();
    auto typedefs = checker.Typedefs();
    auto funcdefs = checker.Funcdefs();
    BytecodeCompiler compiler(diagnostics);
    std::vector<AstNode*> definitionRoots;
    definitionRoots.reserve(image_->definitionTrees.size());
    for (const auto& definitions : image_->definitionTrees)
        definitionRoots.push_back(definitions->root);
    BytecodeModule candidate = compiler.Compile(
        tree.root, functions, classes, globals, enums, funcdefs, definitionRoots);
    if (diagnostics.HasErrors()) return nullptr;
    candidate.globalInitializer = image_->bytecode.globalInitializer;

    const std::size_t callableOffset = image_->bytecode.callables.size();
    std::vector<CallableRef> dynamicCallables = std::move(candidate.callables);
    candidate.callables = image_->bytecode.callables;
    candidate.callables.insert(candidate.callables.end(),
                               std::make_move_iterator(dynamicCallables.begin()),
                               std::make_move_iterator(dynamicCallables.end()));
    for (auto& function : candidate.functions) {
        const BytecodeFunction* existing = image_->bytecode.FindFunction(function.signature.id);
        if (existing) {
            function = *existing;
            continue;
        }
        if (std::find(dynamicIds.begin(), dynamicIds.end(), function.signature.id) ==
            dynamicIds.end()) continue;
        for (auto& instruction : function.code) {
            if (!UsesCallableDescriptor(instruction.opcode) || instruction.operand < 0) continue;
            instruction.operand += static_cast<std::int32_t>(callableOffset);
        }
    }
    for (const auto& existing : image_->bytecode.functions)
        if (!candidate.FindFunction(existing.signature.id))
            candidate.functions.push_back(existing);
    for (const auto& host : engine_.hostFunctions_)
        if (host.active && IsVisible(host.accessMask, accessMask_))
            candidate.hostFunctions.push_back({host.signature.id, &host});
    for (auto& binding : candidate.globals) {
        if (!binding.signature.host) continue;
        for (const auto& host : engine_.hostProperties_) {
            if (!host.active || !IsVisible(host.accessMask, accessMask_)) continue;
            if (host.signature.id == binding.signature.id) {
                binding.host = &host;
                break;
            }
        }
        if (!binding.host) {
            diagnostics.Report({sectionName}, Severity::Error,
                               "registered global property binding is unavailable");
            return nullptr;
        }
    }
    for (const auto& type : classes) {
        const TypeInfo* linked = engine_.GetTypeInfo(type.name);
        if (linked) candidate.objectTypes.push_back({linked->id, linked});
    }

    std::shared_ptr<ModuleState> state = image_->state;
    if (!state) {
        state = std::make_shared<ModuleState>();
        state->globals.reserve(candidate.globals.size());
        for (const auto& global : candidate.globals) {
            state->globals.push_back(global.host && global.host->storage
                ? *global.host->storage : DefaultGlobalValue(global.signature.type, engine_));
        }
    }
    auto nextImage = std::make_shared<ModuleImage>();
    nextImage->bytecode = std::move(candidate);
    nextImage->finalizerBytecode =
        std::make_shared<BytecodeModule>(nextImage->bytecode);
    nextImage->state = std::move(state);
    nextImage->environment = base;
    nextImage->definitionTrees = image_->definitionTrees;
    nextImage->removedFunctions = image_->removedFunctions;
    if (addToModule) {
        nextImage->environment.functions = functions;
        nextImage->environment.classes = classes;
        nextImage->environment.globals = globals;
        nextImage->environment.enums = enums;
        nextImage->environment.typedefs = typedefs;
        nextImage->environment.funcdefs = funcdefs;
        nextImage->definitionTrees.push_back(
            std::make_shared<SyntaxTree>(std::move(tree)));
    }
    engine_.RegisterModuleImage(nextImage);
    const BytecodeFunction* result = nextImage->bytecode.FindFunction(primaryId);
    if (!result) {
        diagnostics.Report(declaration->token.location, Severity::Error,
                           "compiled function is unavailable");
        return nullptr;
    }
    for (const FunctionId id : dynamicIds) {
        const BytecodeFunction* function = nextImage->bytecode.FindFunction(id);
        if (function) engine_.PublishFunctionMetadata(function->signature, name_);
    }
    dynamicImages_.push_back(nextImage);
    if (addToModule) image_ = std::move(nextImage);
    return result;
}

bool ScriptModule::RemoveFunction(const BytecodeFunction* function) {
    if (!function || function->signature.host || function->signature.method) return false;
    const FunctionId id = function->signature.id;
    if (!id.IsValid() ||
        std::find(image_->removedFunctions.begin(), image_->removedFunctions.end(), id) !=
            image_->removedFunctions.end() ||
        !image_->bytecode.FindFunction(id)) return false;

    auto nextImage = std::make_shared<ModuleImage>(*image_);
    nextImage->removedFunctions.push_back(id);
    auto& functions = nextImage->environment.functions;
    functions.erase(std::remove_if(functions.begin(), functions.end(),
        [id](const FunctionSignature& signature) { return signature.id == id; }),
        functions.end());
    engine_.RegisterModuleImage(nextImage);
    dynamicImages_.push_back(image_);
    image_ = std::move(nextImage);
    return true;
}

bool ScriptModule::SaveBytecode(std::ostream& output) const {
    detail::BytecodeArchive archive;
    archive.bytecode = image_->bytecode;
    archive.environment = image_->environment;
    archive.removedFunctions = image_->removedFunctions;
    archive.definitionTrees = image_->definitionTrees;
    std::string error;
    if (detail::WriteBytecodeArchive(output, archive, error)) return true;
    engine_.ForwardDiagnostic({{"bytecode"}, Severity::Error,
                               "bytecode save failed: " + error});
    return false;
}

bool ScriptModule::LoadBytecode(std::istream& input) {
    detail::BytecodeArchive archive;
    std::string error;
    if (!detail::ReadBytecodeArchive(input, archive, error)) {
        engine_.ForwardDiagnostic({{"bytecode"}, Severity::Error,
                                   "bytecode load failed: " + error});
        return false;
    }

    for (const auto& archivedType : archive.environment.classes) {
        if (!archivedType.host || engine_.GetTypeInfo(archivedType.name)) continue;
        DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) {
            engine_.ForwardDiagnostic(diagnostic);
        });
        const std::string templateProbe = archivedType.name + "@ __instance;";
        Tokenizer tokenizer("bytecode-template", templateProbe, diagnostics);
        Parser parser(tokenizer.ScanAll(), diagnostics);
        for (const auto& registered : engine_.HostTemplateTypes(accessMask_))
            parser.RegisterTemplateType(registered.first, registered.second);
        parser.Parse();
        if (diagnostics.HasErrors() ||
            !engine_.InstantiateTemplateTypes(
                parser.TemplateTypeUses(), accessMask_, diagnostics)) {
            engine_.ForwardDiagnostic({{"bytecode"}, Severity::Error,
                "bytecode load failed: template object types do not match"});
            return false;
        }
    }

    std::unordered_map<std::uint32_t, FunctionId> functionIds;
    std::unordered_map<std::uint32_t, TypeId> typeIds;
    std::unordered_map<std::uint32_t, GlobalId> globalIds;
    bool linked = true;
    const auto sameCallable = [](const FunctionSignature& left,
                                 const FunctionSignature& right) {
        return left.Declaration() == right.Declaration() &&
               left.objectType == right.objectType && left.method == right.method &&
               left.factory == right.factory && left.constructor == right.constructor &&
               left.destructor == right.destructor;
    };
    const auto remapFunction = [&](FunctionSignature& signature) {
        if (!signature.id.IsValid()) return;
        const std::uint32_t old = signature.id.value;
        const auto known = functionIds.find(old);
        if (known != functionIds.end()) { signature.id = known->second; return; }
        FunctionId replacement;
        if (signature.host) {
            for (const auto& host : engine_.hostFunctions_)
                if (host.active && sameCallable(signature, host.signature)) replacement = host.signature.id;
            if (!replacement.IsValid()) { linked = false; return; }
        } else {
            replacement = engine_.GetOrCreateFunctionId(
                name_ + "\n$bytecode:" + std::to_string(old) + "\n" +
                (signature.method ? signature.objectType + "::" : std::string{}) +
                signature.Declaration());
        }
        functionIds.emplace(old, replacement);
        signature.id = replacement;
    };
    for (auto& function : archive.environment.functions) remapFunction(function);
    for (auto& type : archive.environment.classes)
        for (auto& method : type.methods) remapFunction(method);
    for (auto& function : archive.bytecode.functions) remapFunction(function.signature);

    const auto remapType = [&](TypeId& id, std::string_view name, bool host) {
        if (!id.IsValid()) return;
        const std::uint32_t old = id.value;
        const auto known = typeIds.find(old);
        if (known != typeIds.end()) { id = known->second; return; }
        TypeId replacement;
        if (host) {
            const TypeInfo* current = engine_.GetTypeInfo(name);
            if (current) replacement = current->id;
            const auto active = [&](TypeId candidate) {
                const auto control = engine_.typeControls_.find(candidate.value);
                return control != engine_.typeControls_.end() && control->second.active;
            };
            for (const auto& type : engine_.hostEnums_)
                if (type.name == name && active(type.id)) replacement = type.id;
            for (const auto& type : engine_.hostTypedefs_)
                if (type.name == name && active(type.id)) replacement = type.id;
            for (const auto& type : engine_.hostFuncdefs_)
                if (type.name == name && active(type.id)) replacement = type.id;
            if (!replacement.IsValid()) { linked = false; return; }
        } else replacement = engine_.GetOrCreateTypeId(name);
        typeIds.emplace(old, replacement);
        id = replacement;
    };
    for (auto& type : archive.environment.classes) remapType(type.id, type.name, type.host);
    for (auto& type : archive.environment.enums) {
        bool host = false;
        for (const auto& current : engine_.hostEnums_) {
            const auto control = engine_.typeControls_.find(current.id.value);
            if (control == engine_.typeControls_.end() || !control->second.active) continue;
            if (current.name != type.name) continue;
            host = true;
            if (current.values.size() != type.values.size()) linked = false;
            for (std::size_t index = 0;
                 index < current.values.size() && index < type.values.size(); ++index)
                if (current.values[index].name != type.values[index].name ||
                    current.values[index].value != type.values[index].value) linked = false;
        }
        remapType(type.id, type.name, host);
    }
    for (auto& type : archive.environment.typedefs) {
        bool host = false;
        for (const auto& current : engine_.hostTypedefs_) {
            const auto control = engine_.typeControls_.find(current.id.value);
            if (control == engine_.typeControls_.end() || !control->second.active) continue;
            if (current.name != type.name) continue;
            host = true;
            if (current.underlyingType != type.underlyingType) linked = false;
        }
        remapType(type.id, type.name, host);
    }
    for (auto& type : archive.environment.funcdefs) {
        bool host = false;
        for (const auto& current : engine_.hostFuncdefs_) {
            const auto control = engine_.typeControls_.find(current.id.value);
            if (control == engine_.typeControls_.end() || !control->second.active) continue;
            if (current.name != type.name) continue;
            host = true;
            if (current.signature.Declaration() != type.signature.Declaration()) linked = false;
        }
        remapType(type.id, type.name, host);
    }
    for (auto& type : archive.bytecode.funcdefs) {
        const auto found = typeIds.find(type.id.value);
        if (type.id.IsValid() && found != typeIds.end()) type.id = found->second;
        else if (type.id.IsValid()) linked = false;
    }

    const auto remapGlobal = [&](GlobalSignature& signature) {
        if (!signature.id.IsValid()) return;
        const std::uint32_t old = signature.id.value;
        const auto known = globalIds.find(old);
        if (known != globalIds.end()) { signature.id = known->second; return; }
        GlobalId replacement;
        if (signature.host) {
            for (const auto& host : engine_.hostProperties_)
                if (host.active && host.signature.name == signature.name &&
                    host.signature.type == signature.type &&
                    host.signature.isConst == signature.isConst)
                    replacement = host.signature.id;
            if (!replacement.IsValid()) { linked = false; return; }
        } else replacement = engine_.GetOrCreateGlobalId(name_ + "\n" + signature.name);
        globalIds.emplace(old, replacement);
        signature.id = replacement;
    };
    for (auto& global : archive.environment.globals) remapGlobal(global);
    for (auto& global : archive.bytecode.globals) remapGlobal(global.signature);

    const auto mapFunctionId = [&](FunctionId& id) {
        if (!id.IsValid()) return;
        const auto found = functionIds.find(id.value);
        if (found == functionIds.end()) linked = false;
        else id = found->second;
    };
    const auto mapTypeId = [&](TypeId& id) {
        if (!id.IsValid()) return;
        const auto found = typeIds.find(id.value);
        if (found == typeIds.end()) linked = false;
        else id = found->second;
    };
    for (auto& callable : archive.bytecode.callables) {
        mapFunctionId(callable.function);
        mapTypeId(callable.objectType);
        mapTypeId(callable.signatureType);
    }
    for (auto& dispatch : archive.bytecode.virtualDispatch) {
        mapTypeId(dispatch.concreteType); mapTypeId(dispatch.interfaceType);
        mapFunctionId(dispatch.implementation);
    }
    for (auto& destructor : archive.bytecode.destructors) {
        mapTypeId(destructor.first); mapFunctionId(destructor.second);
    }
    for (auto& id : archive.removedFunctions) mapFunctionId(id);

    const auto remapValue = [&](Value& value) {
        const auto& raw = value.Raw();
        if (const auto* handle = std::get_if<FunctionHandle>(&raw)) {
            FunctionHandle replacement = *handle;
            mapFunctionId(replacement.function);
            mapTypeId(replacement.signature);
            mapTypeId(replacement.dispatchType);
            value = Value(std::move(replacement));
        } else if (const auto* reference = std::get_if<ReferenceStorage>(&raw)) {
            ReferenceStorage replacement = *reference;
            if (replacement.kind == ReferenceKind::Global) {
                const auto found = globalIds.find(replacement.slot);
                if (found == globalIds.end()) linked = false;
                else replacement.slot = found->second.value;
            }
            value = Value(std::move(replacement));
        } else if (const auto* host = std::get_if<HostValueStorage>(&raw)) {
            const TypeInfo* type = engine_.GetTypeInfo(host->typeName);
            if (!type || !type->valueType) linked = false;
            else value = type->defaultValue;
        }
    };
    const auto remapFunctionBody = [&](BytecodeFunction& function) {
        for (auto& constant : function.constants) remapValue(constant);
        for (auto& instruction : function.code) {
            if (UsesCallableDescriptor(instruction.opcode)) {
                if (instruction.operand < 0 ||
                    static_cast<std::size_t>(instruction.operand) >=
                        archive.bytecode.callables.size()) linked = false;
            } else if (instruction.opcode == OpCode::LoadGlobal ||
                       instruction.opcode == OpCode::StoreGlobal ||
                       instruction.opcode == OpCode::MakeGlobalReference) {
                const auto found = globalIds.find(static_cast<std::uint32_t>(instruction.operand));
                if (instruction.operand < 0 || found == globalIds.end()) linked = false;
                else instruction.operand = static_cast<std::int32_t>(found->second.value);
            } else if (instruction.opcode == OpCode::CastObject ||
                       instruction.opcode == OpCode::NewObject) {
                const auto found = typeIds.find(static_cast<std::uint32_t>(instruction.operand));
                if (instruction.operand < 0 || found == typeIds.end()) linked = false;
                else instruction.operand = static_cast<std::int32_t>(found->second.value);
            }
        }
    };
    for (auto& function : archive.bytecode.functions) remapFunctionBody(function);
    remapFunctionBody(archive.bytecode.globalInitializer);
    if (!linked) {
        engine_.ForwardDiagnostic({{"bytecode"}, Severity::Error,
                                   "bytecode load failed: registered symbols do not match"});
        return false;
    }

    std::vector<const TypeInfo*> linkedTypes;
    const auto currentHostTypes = engine_.HostTypeSignatures(~std::uint32_t{0});
    for (auto& type : archive.environment.classes) {
        if (type.host) {
            const TypeInfo* current = engine_.GetTypeInfo(type.name);
            if (!current || current->valueType != type.valueType) linked = false;
            else {
                const ClassSignature* currentSignature = nullptr;
                for (const auto& candidate : currentHostTypes)
                    if (candidate.name == type.name) currentSignature = &candidate;
                if (!currentSignature || currentSignature->fields.size() != type.fields.size())
                    linked = false;
                if (currentSignature) {
                    for (std::size_t index = 0;
                         index < currentSignature->fields.size() && index < type.fields.size();
                         ++index) {
                        const auto& left = currentSignature->fields[index];
                        const auto& right = type.fields[index];
                        if (left.name != right.name || left.type != right.type ||
                            left.isConst != right.isConst) linked = false;
                    }
                }
                type.defaultValue = current->defaultValue;
                linkedTypes.push_back(current);
            }
        } else {
            const TypeInfo* current = engine_.RegisterScriptType(type);
            if (!current) linked = false;
            else linkedTypes.push_back(current);
        }
    }
    if (!linked) {
        engine_.ForwardDiagnostic({{"bytecode"}, Severity::Error,
                                   "bytecode load failed: object types do not match"});
        return false;
    }
    for (const auto& type : archive.environment.classes)
        if (!type.host) engine_.LinkScriptType(type);
    for (const auto& host : engine_.hostFunctions_)
        if (host.active) archive.bytecode.hostFunctions.push_back({host.signature.id, &host});
    for (auto& binding : archive.bytecode.globals) {
        if (!binding.signature.host) continue;
        for (const auto& host : engine_.hostProperties_)
            if (host.active && host.signature.id == binding.signature.id) binding.host = &host;
        if (!binding.host) linked = false;
    }
    for (const auto* type : linkedTypes)
        archive.bytecode.objectTypes.push_back({type->id, type});
    if (!linked) {
        engine_.ForwardDiagnostic({{"bytecode"}, Severity::Error,
                                   "bytecode load failed: host bindings are unavailable"});
        return false;
    }

    auto state = std::make_shared<ModuleState>();
    state->globals.reserve(archive.bytecode.globals.size());
    for (const auto& global : archive.bytecode.globals) {
        state->globals.push_back(global.host && global.host->storage
            ? *global.host->storage : DefaultGlobalValue(global.signature.type, engine_));
    }
    VirtualMachine initializer;
    auto finalizerModule = std::make_shared<BytecodeModule>(archive.bytecode);
    initializer.SetFinalizerContext(&engine_, finalizerModule, state,
                                    [this] { engine_.DrainFinalizers(); });
    const auto initialized = initializer.Execute(
        archive.bytecode.globalInitializer, {}, &archive.bytecode, state.get());
    engine_.DrainFinalizers();
    if (initialized.state != ExecutionState::Finished) {
        engine_.ForwardDiagnostic({initialized.location, Severity::Error,
            "bytecode load failed: global initialization failed: " + initialized.exception});
        return false;
    }

    for (const auto& type : archive.environment.classes)
        if (!type.host) engine_.PublishObjectMetadata(type);
    for (const auto& type : archive.environment.enums) {
        bool host = false;
        for (const auto& current : engine_.hostEnums_) host = host || current.name == type.name;
        if (!host) engine_.PublishEnumMetadata(type, false);
    }
    for (const auto& type : archive.environment.typedefs) {
        bool host = false;
        for (const auto& current : engine_.hostTypedefs_) host = host || current.name == type.name;
        if (!host) engine_.PublishTypedefMetadata(type, false);
    }
    for (const auto& type : archive.environment.funcdefs) {
        bool host = false;
        for (const auto& current : engine_.hostFuncdefs_) host = host || current.name == type.name;
        if (!host) engine_.PublishFuncdefMetadata(type, false);
    }
    for (const auto& function : archive.environment.functions)
        if (!function.host) engine_.PublishFunctionMetadata(function, name_);
    for (const auto& type : archive.environment.classes)
        if (!type.host) for (const auto& method : type.methods)
            engine_.PublishFunctionMetadata(method, name_);
    for (const auto& global : archive.environment.globals)
        if (!global.host) engine_.PublishGlobalMetadata(global, name_);

    auto nextImage = std::make_shared<ModuleImage>();
    nextImage->bytecode = std::move(archive.bytecode);
    nextImage->finalizerBytecode = std::move(finalizerModule);
    nextImage->state = std::move(state);
    nextImage->environment = std::move(archive.environment);
    nextImage->removedFunctions = std::move(archive.removedFunctions);
    nextImage->definitionTrees = std::move(archive.definitionTrees);
    engine_.RegisterModuleImage(nextImage);
    dynamicImages_.push_back(image_);
    image_ = std::move(nextImage);
    sections_.clear();
    return true;
}

const GlobalMetadata* ScriptModule::GetGlobalMetadataById(GlobalId id) const {
    for (const auto& binding : image_->bytecode.globals)
        if (!binding.signature.host && binding.signature.id == id)
            return engine_.FindGlobalMetadata(id);
    return nullptr;
}

const GlobalMetadata* ScriptModule::GetGlobalMetadataByName(std::string_view name) const {
    for (const auto& binding : image_->bytecode.globals)
        if (!binding.signature.host && binding.signature.name == name)
            return engine_.FindGlobalMetadata(binding.signature.id);
    return nullptr;
}

const GlobalMetadata* ScriptModule::GetGlobalMetadataByDecl(
    std::string_view declaration) const {
    for (const auto& binding : image_->bytecode.globals)
        if (!binding.signature.host && binding.signature.Declaration() == declaration)
            return engine_.FindGlobalMetadata(binding.signature.id);
    return nullptr;
}

const BytecodeModule& ScriptModule::Bytecode() const { return image_->bytecode; }

std::uint32_t ScriptModule::SetAccessMask(std::uint32_t accessMask) {
    const auto previous = accessMask_;
    accessMask_ = accessMask;
    return previous;
}
std::uint32_t ScriptModule::GetAccessMask() const { return accessMask_; }
bool ScriptModule::SetDefaultNamespace(std::string nameSpace) {
    if (!IsValidNamespace(nameSpace)) return false;
    defaultNamespace_ = std::move(nameSpace);
    return true;
}
const std::string& ScriptModule::GetDefaultNamespace() const { return defaultNamespace_; }

ScriptContext::ScriptContext(ScriptEngine& engine) : engine_(engine) {
    vm_.SetLineCallback([this](const SourceLocation& location) {
        if (lineCallback_) lineCallback_(*this, location);
    });
}

bool ScriptContext::Prepare(const BytecodeFunction* function) {
    image_.reset();
    function_ = nullptr;
    if (!function) {
        result_ = {};
        result_.state = ExecutionState::Exception;
        result_.exception = "cannot prepare a null function";
        return false;
    }
    image_ = engine_.FindModuleImage(function);
    if (!image_) {
        result_ = {};
        result_.state = ExecutionState::Exception;
        result_.exception = "function does not belong to a live module image";
        return false;
    }
    function_ = function;
    arguments_.assign(function_->signature.parameters.size(), Value{});
    result_ = {};
    result_.state = ExecutionState::Prepared;
    return true;
}

bool ScriptContext::SetArgInt(std::size_t index, std::int32_t value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgFloat(std::size_t index, float value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgDouble(std::size_t index, double value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgBool(std::size_t index, bool value) { return SetArgument(index, Value(value)); }
bool ScriptContext::SetArgString(std::size_t index, std::string value) { return SetArgument(index, Value(std::move(value))); }
bool ScriptContext::SetArgObject(std::size_t index, ObjectHandle value) { return SetArgument(index, Value(std::move(value))); }
bool ScriptContext::SetArgValue(std::size_t index, Value value) {
    return SetArgument(index, std::move(value));
}

ExecutionState ScriptContext::Execute() {
    if (!function_) {
        result_ = {};
        result_.state = ExecutionState::Exception;
        result_.exception = "context has no prepared function";
        return result_.state;
    }
    if (result_.state == ExecutionState::Prepared) {
        vm_.SetFinalizerContext(&engine_, image_->finalizerBytecode, image_->state,
                                [this] { engine_.DrainFinalizers(); });
        if (!vm_.Prepare(*function_, arguments_, &image_->bytecode, image_->state.get())) {
            result_ = vm_.Continue();
            engine_.DrainFinalizers();
            return result_.state;
        }
    }
    result_.state = ExecutionState::Active;
    result_ = vm_.Continue();
    engine_.DrainFinalizers();
    return result_.state;
}

void ScriptContext::Suspend() { vm_.RequestSuspend(); }
void ScriptContext::Abort() {
    vm_.Abort();
    engine_.DrainFinalizers();
    result_.state = ExecutionState::Aborted;
}
void ScriptContext::SetLineCallback(LineCallback callback) { lineCallback_ = std::move(callback); }
ExecutionState ScriptContext::GetState() const { return result_.state; }
const Value& ScriptContext::GetReturnValue() const { return result_.returnValue; }
std::int32_t ScriptContext::GetReturnInt() const { return result_.returnValue.As<std::int32_t>(); }
float ScriptContext::GetReturnFloat() const { return result_.returnValue.As<float>(); }
double ScriptContext::GetReturnDouble() const { return result_.returnValue.As<double>(); }
const std::string& ScriptContext::GetExceptionString() const { return result_.exception; }
const SourceLocation& ScriptContext::GetExceptionLocation() const { return result_.location; }
const std::vector<StackFrameInfo>& ScriptContext::GetCallStack() const { return result_.callStack; }
std::size_t ScriptContext::GetCallStackSize() const {
    if (result_.state == ExecutionState::Exception) return result_.callStack.size();
    return result_.state == ExecutionState::Active || result_.state == ExecutionState::Suspended
        ? vm_.GetCallStackSize() : 0;
}
const BytecodeFunction* ScriptContext::GetFunction(std::size_t stackLevel) const {
    if (result_.state == ExecutionState::Exception)
        return stackLevel < result_.callStack.size()
            ? result_.callStack[stackLevel].function : nullptr;
    return result_.state == ExecutionState::Active || result_.state == ExecutionState::Suspended
        ? vm_.GetFunction(stackLevel) : nullptr;
}
SourceLocation ScriptContext::GetInstructionLocation(std::size_t stackLevel) const {
    if (result_.state == ExecutionState::Exception)
        return stackLevel < result_.callStack.size()
            ? result_.callStack[stackLevel].location : SourceLocation{};
    return result_.state == ExecutionState::Active || result_.state == ExecutionState::Suspended
        ? vm_.GetInstructionLocation(stackLevel) : SourceLocation{};
}
std::vector<LocalVariableInfo> ScriptContext::GetLocals(std::size_t stackLevel) const {
    if (result_.state == ExecutionState::Exception)
        return stackLevel < result_.callStack.size()
            ? result_.callStack[stackLevel].locals : std::vector<LocalVariableInfo>{};
    return result_.state == ExecutionState::Active || result_.state == ExecutionState::Suspended
        ? vm_.GetLocals(stackLevel) : std::vector<LocalVariableInfo>{};
}

bool ScriptContext::SetArgument(std::size_t index, Value value) {
    if (!function_ || index >= arguments_.size() || result_.state != ExecutionState::Prepared) return false;
    const DataType expected = function_->signature.parameters[index];
    if (value.Type().IsInteger() && expected.IsInteger()) value = ConvertInteger(value, expected);
    else if (value.Type().IsSignedInteger() && expected == DataType::Float())
        value = Value(static_cast<float>(value.SignedInteger()));
    else if (value.Type().IsUnsignedInteger() && expected == DataType::Float())
        value = Value(static_cast<float>(value.UnsignedInteger()));
    else if (value.Type().IsSignedInteger() && expected == DataType::Double())
        value = Value(static_cast<double>(value.SignedInteger()));
    else if (value.Type().IsUnsignedInteger() && expected == DataType::Double())
        value = Value(static_cast<double>(value.UnsignedInteger()));
    else if (value.Type() == DataType::Float() && expected == DataType::Double())
        value = Value(static_cast<double>(value.As<float>()));
    else if (value.Type() == DataType::Double() && expected == DataType::Float())
        value = Value(static_cast<float>(value.As<double>()));
    else if (value.Type() != expected) {
        if (value.Type().kind == TypeKind::Object && value.Type().objectName == "<null>" && expected.isHandle) {
            arguments_[index] = std::move(value);
            return true;
        }
        if (value.Type().kind != TypeKind::Object || expected.kind != TypeKind::Object ||
            !value.As<ObjectHandle>()) return false;
        const auto* scriptObject = dynamic_cast<ScriptObject*>(value.As<ObjectHandle>().Get());
        if (!scriptObject || (!scriptObject->Implements(expected.objectName) &&
                              !scriptObject->IsA(expected.objectName))) return false;
    }
    arguments_[index] = std::move(value);
    return true;
}

void ScriptEngine::SetMessageCallback(MessageCallback callback) { messageCallback_ = std::move(callback); }

std::uint32_t ScriptEngine::SetDefaultAccessMask(std::uint32_t accessMask) {
    const auto previous = defaultAccessMask_;
    defaultAccessMask_ = accessMask;
    return previous;
}
std::uint32_t ScriptEngine::GetDefaultAccessMask() const { return defaultAccessMask_; }
bool ScriptEngine::SetDefaultNamespace(std::string nameSpace) {
    if (!IsValidNamespace(nameSpace)) return false;
    defaultNamespace_ = std::move(nameSpace);
    return true;
}
const std::string& ScriptEngine::GetDefaultNamespace() const { return defaultNamespace_; }
bool ScriptEngine::BeginConfigGroup(std::string name) {
    if (name.empty() || !currentConfigGroup_.empty() || configGroups_.count(name)) return false;
    currentConfigGroup_ = std::move(name);
    configGroups_.insert(currentConfigGroup_);
    return true;
}
bool ScriptEngine::EndConfigGroup() {
    if (currentConfigGroup_.empty()) return false;
    currentConfigGroup_.clear();
    return true;
}
bool ScriptEngine::RemoveConfigGroup(std::string_view name) {
    if (name.empty() || !currentConfigGroup_.empty() || !configGroups_.count(std::string(name)))
        return false;
    const auto functionInGroup = [&](FunctionId id) {
        for (const auto& host : hostFunctions_)
            if (host.active && host.signature.id == id && host.configGroup == name) return true;
        return false;
    };
    const auto globalInGroup = [&](GlobalId id) {
        for (const auto& host : hostProperties_)
            if (host.active && host.signature.id == id && host.configGroup == name) return true;
        return false;
    };
    const auto typeInGroup = [&](TypeId id) {
        const auto found = typeControls_.find(id.value);
        return found != typeControls_.end() && found->second.active &&
               found->second.configGroup == name;
    };
    const auto environmentUsesGroup = [&](const ModuleCompilationEnvironment& environment) {
        for (const auto& function : environment.functions)
            if (function.host && functionInGroup(function.id)) return true;
        for (const auto& global : environment.globals)
            if (global.host && globalInGroup(global.id)) return true;
        for (const auto& type : environment.classes) {
            if (!type.host) continue;
            if (typeInGroup(type.id)) return true;
            for (const auto& method : type.methods)
                if (functionInGroup(method.id)) return true;
            for (const auto& field : type.fields)
                for (const auto& property : hostObjectProperties_)
                    if (property.active && property.configGroup == name &&
                        property.signature.objectType == type.name &&
                        property.signature.name == field.name) return true;
        }
        for (const auto& type : environment.enums) if (typeInGroup(type.id)) return true;
        for (const auto& type : environment.typedefs) if (typeInGroup(type.id)) return true;
        for (const auto& type : environment.funcdefs) if (typeInGroup(type.id)) return true;
        return false;
    };
    std::unordered_set<const ModuleImage*> checked;
    for (const auto& entry : modules_) {
        if (!entry.second->image_ || !entry.second->image_->state) continue;
        checked.insert(entry.second->image_.get());
        if (environmentUsesGroup(entry.second->image_->environment)) return false;
    }
    for (const auto& entry : moduleImages_) {
        const auto image = entry.second.lock();
        if (!image || !image->state || !checked.insert(image.get()).second) continue;
        if (environmentUsesGroup(image->environment)) return false;
    }
    for (auto& host : hostFunctions_) if (host.configGroup == name) host.active = false;
    for (auto& host : hostProperties_) if (host.configGroup == name) host.active = false;
    for (auto& host : hostObjectProperties_) if (host.configGroup == name) host.active = false;
    for (auto& entry : objectTypes_)
        if (entry.second->configGroup == name) entry.second->active = false;
    for (auto& entry : typeControls_)
        if (entry.second.configGroup == name) entry.second.active = false;
    configGroups_.erase(std::string(name));
    return true;
}

ScriptEngine::~ScriptEngine() {
    for (auto& entry : modules_) {
        if (!entry.second->image_ || !entry.second->image_->state) continue;
        for (auto& global : entry.second->image_->state->globals) global = Value{};
    }
    DrainFinalizers();
    garbageCollector_.Collect();
    DrainFinalizers();
    modules_.clear();
    DrainFinalizers();
}

bool ScriptEngine::RegisterGlobalFunction(std::string declaration, GenericFunction callback) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    auto signature = ParseFunctionDeclaration(declaration, diagnostics);
    if (!signature || !callback) return false;
    signature->name = QualifyName(defaultNamespace_, std::move(signature->name));
    ResolveRegisteredTypes(*signature);
    if (signature->readOnlyMethod) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "global functions cannot use the method const qualifier");
        return false;
    }
    if (signature->returnsReference) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "host return references require registered property storage");
        return false;
    }
    for (const auto& existing : hostFunctions_) {
        if (!existing.active) continue;
        if (existing.signature.factory || existing.signature.method) continue;
        if (existing.signature.name == signature->name &&
            existing.signature.parameters == signature->parameters) {
            diagnostics.Report({"registration"}, Severity::Error,
                               "duplicate global function '" + signature->Declaration() + "'");
            return false;
        }
    }
    signature->id = GetOrCreateFunctionId("$host\n" + signature->Declaration());
    hostFunctions_.push_back({std::move(*signature), std::move(callback),
                              defaultAccessMask_, currentConfigGroup_, true});
    PublishFunctionMetadata(hostFunctions_.back().signature);
    return true;
}

const TypeInfo* ScriptEngine::RegisterObjectType(std::string name, bool garbageCollected) {
    if (name.find('<') != std::string::npos)
        return RegisterTemplateType(std::move(name), {}, {}, garbageCollected);
    name = QualifyName(defaultNamespace_, std::move(name));
    if (name.empty() || HasRegisteredType(name)) return nullptr;
    auto type = std::make_unique<TypeInfo>();
    type->name = name;
    type->id = GetOrCreateTypeId(name);
    type->host = true;
    type->collector = garbageCollected ? &garbageCollector_ : nullptr;
    type->accessMask = defaultAccessMask_;
    type->configGroup = currentConfigGroup_;
    const TypeInfo* result = type.get();
    objectTypes_.insert_or_assign(std::move(name), std::move(type));
    ClassSignature signature;
    signature.name = result->name;
    signature.id = result->id;
    signature.host = true;
    typeControls_[result->id.value] = {defaultAccessMask_, currentConfigGroup_, true};
    PublishObjectMetadata(signature);
    return result;
}

const TypeInfo* ScriptEngine::RegisterTemplateType(std::string declaration,
                                                    TemplateValidator validator,
                                                    TemplateInstanceCallback instanceCallback,
                                                    bool garbageCollected) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) {
        ForwardDiagnostic(diagnostic);
    });
    auto parsed = ParseTemplateTypeDeclaration(declaration, diagnostics);
    if (!parsed) return nullptr;
    parsed->first = QualifyName(defaultNamespace_, std::move(parsed->first));
    if (parsed->first.empty() || HasRegisteredType(parsed->first)) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "duplicate or invalid registered template type '" +
                               parsed->first + "'");
        return nullptr;
    }
    for (const auto& registered : templateTypes_)
        if (registered.name == parsed->first && registered.definition &&
            registered.definition->active) {
            diagnostics.Report({"registration"}, Severity::Error,
                               "duplicate registered template type '" + parsed->first + "'");
            return nullptr;
        }

    std::string patternName = parsed->first + "<";
    for (std::size_t index = 0; index < parsed->second.size(); ++index) {
        if (index) patternName += ",";
        patternName += parsed->second[index];
    }
    patternName += ">";
    auto type = std::make_unique<TypeInfo>();
    type->name = patternName;
    type->id = GetOrCreateTypeId(patternName);
    type->host = true;
    type->templateDefinition = true;
    type->templateBase = parsed->first;
    type->templateParameters = parsed->second;
    type->accessMask = defaultAccessMask_;
    type->configGroup = currentConfigGroup_;
    TypeInfo* result = type.get();
    objectTypes_.insert_or_assign(patternName, std::move(type));
    templateTypes_.push_back(
        {parsed->first, parsed->second, result, std::move(validator),
         std::move(instanceCallback), garbageCollected});
    typeControls_[result->id.value] = {defaultAccessMask_, currentConfigGroup_, true};
    ClassSignature signature;
    signature.name = result->name;
    signature.id = result->id;
    signature.host = true;
    PublishObjectMetadata(signature);
    return result;
}

const TypeInfo* ScriptEngine::RegisterValueType(std::string name, Value defaultValue) {
    name = QualifyName(defaultNamespace_, std::move(name));
    if (name.empty() || HasRegisteredType(name) ||
        defaultValue.Type() != DataType::Object(name, false)) return nullptr;
    auto type = std::make_unique<TypeInfo>();
    type->name = name;
    type->id = GetOrCreateTypeId(name);
    type->host = true;
    type->valueType = true;
    type->accessMask = defaultAccessMask_;
    type->configGroup = currentConfigGroup_;
    type->defaultValue = std::move(defaultValue);
    const TypeInfo* result = type.get();
    objectTypes_.insert_or_assign(std::move(name), std::move(type));
    ClassSignature signature;
    signature.name = result->name;
    signature.id = result->id;
    signature.host = true;
    signature.valueType = true;
    signature.defaultValue = result->defaultValue;
    typeControls_[result->id.value] = {defaultAccessMask_, currentConfigGroup_, true};
    PublishObjectMetadata(signature);
    return result;
}

bool ScriptEngine::RegisterEnum(std::string name) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    name = QualifyName(defaultNamespace_, std::move(name));
    if (name.empty() || HasRegisteredType(name)) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "duplicate or invalid registered enum '" + name + "'");
        return false;
    }
    hostEnums_.erase(std::remove_if(hostEnums_.begin(), hostEnums_.end(),
        [&](const auto& existing) {
            const auto control = typeControls_.find(existing.id.value);
            return existing.name == name && control != typeControls_.end() && !control->second.active;
        }), hostEnums_.end());
    hostEnums_.push_back({std::move(name), {}, {}});
    hostEnums_.back().id = GetOrCreateTypeId(hostEnums_.back().name);
    typeControls_[hostEnums_.back().id.value] = {defaultAccessMask_, currentConfigGroup_, true};
    PublishEnumMetadata(hostEnums_.back(), true);
    return true;
}

bool ScriptEngine::RegisterEnumValue(std::string enumName, std::string valueName,
                                     std::int32_t value) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    enumName = QualifyName(defaultNamespace_, std::move(enumName));
    const auto type = std::find_if(hostEnums_.begin(), hostEnums_.end(),
        [&](const auto& candidate) { return candidate.name == enumName; });
    if (type == hostEnums_.end()) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "enum type '" + enumName + "' is not registered");
        return false;
    }
    if (valueName.empty()) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "registered enum value name cannot be empty");
        return false;
    }
    const std::string nameSpace = NamespaceOf(enumName);
    for (const auto& registered : hostEnums_) {
        if (NamespaceOf(registered.name) != nameSpace) continue;
        for (const auto& existing : registered.values) {
            if (existing.name != valueName) continue;
            diagnostics.Report({"registration"}, Severity::Error,
                               "duplicate registered enum value '" + valueName + "'");
            return false;
        }
    }
    type->values.push_back({std::move(valueName), value});
    PublishEnumMetadata(*type, true);
    return true;
}

bool ScriptEngine::RegisterTypedef(std::string name, DataType underlyingType) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    name = QualifyName(defaultNamespace_, std::move(name));
    const bool primitive = underlyingType == DataType::Bool() || underlyingType.IsInteger() ||
        underlyingType == DataType::Float() || underlyingType == DataType::Double();
    if (name.empty() || HasRegisteredType(name) || !primitive ||
        underlyingType.kind == TypeKind::Enum) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "registered typedef requires a unique name and primitive type");
        return false;
    }
    hostTypedefs_.erase(std::remove_if(hostTypedefs_.begin(), hostTypedefs_.end(),
        [&](const auto& existing) {
            const auto control = typeControls_.find(existing.id.value);
            return existing.name == name && control != typeControls_.end() && !control->second.active;
        }), hostTypedefs_.end());
    hostTypedefs_.push_back({std::move(name), std::move(underlyingType), {}});
    hostTypedefs_.back().id = GetOrCreateTypeId(hostTypedefs_.back().name);
    typeControls_[hostTypedefs_.back().id.value] = {defaultAccessMask_, currentConfigGroup_, true};
    PublishTypedefMetadata(hostTypedefs_.back(), true);
    return true;
}

bool ScriptEngine::RegisterFuncdef(std::string declaration) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    auto signature = ParseFunctionDeclaration(declaration, diagnostics);
    if (!signature) return false;
    signature->name = QualifyName(defaultNamespace_, std::move(signature->name));
    ResolveRegisteredTypes(*signature);
    if (signature->name.empty() || HasRegisteredType(signature->name) ||
        signature->readOnlyMethod) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "duplicate or invalid registered funcdef '" + signature->name + "'");
        return false;
    }
    hostFuncdefs_.erase(std::remove_if(hostFuncdefs_.begin(), hostFuncdefs_.end(),
        [&](const auto& existing) {
            const auto control = typeControls_.find(existing.id.value);
            return existing.name == signature->name && control != typeControls_.end() && !control->second.active;
        }), hostFuncdefs_.end());
    signature->host = false;
    FuncdefSignature type{signature->name, std::move(*signature), {}, {}};
    type.id = GetOrCreateTypeId(type.name);
    typeControls_[type.id.value] = {defaultAccessMask_, currentConfigGroup_, true};
    hostFuncdefs_.push_back(std::move(type));
    PublishFuncdefMetadata(hostFuncdefs_.back(), true);
    return true;
}

bool ScriptEngine::RegisterObjectFactory(std::string typeName, std::string declaration,
                                         GenericFunction callback) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    typeName = QualifyName(defaultNamespace_, std::move(typeName));
    const auto type = objectTypes_.find(typeName);
    if (type == objectTypes_.end() || !type->second->host || type->second->valueType) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "factory type '" + typeName + "' is not a registered reference type");
        return false;
    }
    auto signature = ParseFunctionDeclaration(declaration, diagnostics);
    if (!signature || !callback) {
        if (signature && !callback)
            diagnostics.Report({"registration"}, Severity::Error,
                               "object factory callback cannot be empty");
        return false;
    }
    ResolveRegisteredTypes(*signature);
    const DataType expected = DataType::Object(typeName, true);
    if (signature->name != "f" || signature->returnType != expected ||
        signature->returnsReference || signature->readOnlyMethod) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "factory declaration must have the form '" + typeName + "@ f(...)'");
        return false;
    }
    for (const auto& existing : hostFunctions_) {
        if (!existing.active) continue;
        if (!existing.signature.factory || existing.signature.objectType != typeName ||
            existing.signature.parameters != signature->parameters) continue;
        diagnostics.Report({"registration"}, Severity::Error,
                           "duplicate object factory '" + declaration + "'");
        return false;
    }
    signature->factory = true;
    signature->objectType = typeName;
    signature->id = GetOrCreateFunctionId("$factory\n" + typeName + "\n" +
                                          signature->Declaration());
    hostFunctions_.push_back({std::move(*signature), std::move(callback),
                              defaultAccessMask_, currentConfigGroup_, true});
    PublishFunctionMetadata(hostFunctions_.back().signature);
    for (const auto& registered : HostTypeSignatures(~std::uint32_t{0}))
        if (registered.name == typeName) PublishObjectMetadata(registered);
    return true;
}

bool ScriptEngine::RegisterObjectMethod(std::string typeName, std::string declaration,
                                        GenericFunction callback) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    typeName = QualifyName(defaultNamespace_, std::move(typeName));
    const auto type = objectTypes_.find(typeName);
    if (type == objectTypes_.end() || !type->second->host) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "method type '" + typeName + "' is not a registered host type");
        return false;
    }
    auto signature = ParseFunctionDeclaration(declaration, diagnostics);
    if (!signature || !callback) {
        if (signature && !callback)
            diagnostics.Report({"registration"}, Severity::Error,
                               "object method callback cannot be empty");
        return false;
    }
    ResolveRegisteredTypes(*signature);
    if (signature->returnsReference) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "registered object method return references are not supported yet");
        return false;
    }
    if (type->second->valueType && !signature->readOnlyMethod) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "registered value type methods must be const until receiver writeback is supported");
        return false;
    }
    for (const auto& existing : hostFunctions_) {
        if (!existing.active) continue;
        if (!existing.signature.method || existing.signature.objectType != typeName ||
            existing.signature.name != signature->name ||
            existing.signature.parameters != signature->parameters) continue;
        diagnostics.Report({"registration"}, Severity::Error,
                           "duplicate object method '" + typeName + "::" + declaration + "'");
        return false;
    }
    signature->method = true;
    signature->objectType = typeName;
    signature->id = GetOrCreateFunctionId("$host-method\n" + typeName + "\n" +
                                          signature->Declaration());
    hostFunctions_.push_back({std::move(*signature), std::move(callback),
                              defaultAccessMask_, currentConfigGroup_, true});
    PublishFunctionMetadata(hostFunctions_.back().signature);
    for (const auto& registered : HostTypeSignatures(~std::uint32_t{0}))
        if (registered.name == typeName) PublishObjectMetadata(registered);
    return true;
}

bool ScriptEngine::RegisterObjectProperty(std::string typeName, std::string declaration,
                                          GenericPropertyGetter getter,
                                          GenericPropertySetter setter) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    typeName = QualifyName(defaultNamespace_, std::move(typeName));
    const auto type = objectTypes_.find(typeName);
    if (type == objectTypes_.end() || !type->second->host || type->second->valueType) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "property type '" + typeName + "' is not a registered reference type");
        return false;
    }
    auto parsed = ParseGlobalPropertyDeclaration(declaration, diagnostics);
    if (!parsed || !getter) {
        if (parsed && !getter)
            diagnostics.Report({"registration"}, Severity::Error,
                               "object property getter cannot be empty");
        return false;
    }
    parsed->type = ResolveRegisteredType(std::move(parsed->type));
    if (parsed->type.kind == TypeKind::Object &&
        (!parsed->type.isHandle || !GetTypeInfo(parsed->type.objectName))) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "registered object properties require known object handle types");
        return false;
    }
    if (parsed->type.kind == TypeKind::Function ||
        parsed->type.kind == TypeKind::WeakRef || parsed->type.kind == TypeKind::ConstWeakRef ||
        (!parsed->type.IsNumeric() && parsed->type != DataType::Bool() &&
         parsed->type != DataType::String() && parsed->type.kind != TypeKind::Object)) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "registered object property type is not supported yet");
        return false;
    }
    if (parsed->isConst && setter) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "const object property cannot register a setter");
        return false;
    }
    if (!parsed->isConst && !setter) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "mutable object property requires a setter");
        return false;
    }
    for (const auto& existing : hostObjectProperties_) {
        if (!existing.active) continue;
        if (existing.signature.objectType != typeName ||
            existing.signature.name != parsed->name) continue;
        diagnostics.Report({"registration"}, Severity::Error,
                           "duplicate object property '" + typeName + "::" + parsed->name + "'");
        return false;
    }
    FieldSignature signature{parsed->name, parsed->type, typeName, MemberAccess::Public,
                             parsed->isConst, true};
    hostObjectProperties_.push_back(
        {std::move(signature), std::move(getter), std::move(setter),
         defaultAccessMask_, currentConfigGroup_, true});
    const auto* property = &hostObjectProperties_.back();
    type->second->fields.emplace_back(property->signature.name, property->signature.type);
    type->second->hostProperties.push_back(property);
    for (const auto& registered : HostTypeSignatures(~std::uint32_t{0}))
        if (registered.name == typeName) PublishObjectMetadata(registered);
    return true;
}

const TypeInfo* ScriptEngine::GetTypeInfo(std::string_view name) const {
    auto found = objectTypes_.find(std::string(name));
    if (found == objectTypes_.end() && name.find("::") == std::string_view::npos)
        found = objectTypes_.find(QualifyName(defaultNamespace_, std::string(name)));
    if (found == objectTypes_.end()) {
        const std::string requested = QualifyName(defaultNamespace_, std::string(name));
        for (const auto& registered : templateTypes_)
            if (registered.name == requested && registered.definition &&
                registered.definition->active) return registered.definition;
    }
    return found == objectTypes_.end() || !found->second->active ? nullptr : found->second.get();
}

std::size_t ScriptEngine::GetTypeMetadataCount() const {
    std::size_t count = 0;
    for (const auto& metadata : typeMetadata_) {
        const auto control = typeControls_.find(metadata.id.value);
        if (control == typeControls_.end() || control->second.active) ++count;
    }
    return count;
}

const TypeMetadata* ScriptEngine::GetTypeMetadataByIndex(std::size_t index) const {
    for (const auto& metadata : typeMetadata_) {
        const auto control = typeControls_.find(metadata.id.value);
        if (control != typeControls_.end() && !control->second.active) continue;
        if (index-- == 0) return &metadata;
    }
    return nullptr;
}

const TypeMetadata* ScriptEngine::GetTypeMetadataById(TypeId id) const {
    for (const auto& metadata : typeMetadata_)
        if (metadata.id == id) {
            const auto control = typeControls_.find(id.value);
            return control == typeControls_.end() || control->second.active ? &metadata : nullptr;
        }
    return nullptr;
}

const TypeMetadata* ScriptEngine::GetTypeMetadataByName(std::string_view name) const {
    const std::string requested = QualifyName(defaultNamespace_, std::string(name));
    for (const auto& metadata : typeMetadata_)
        if (metadata.name == name || metadata.name == requested) {
            const auto control = typeControls_.find(metadata.id.value);
            return control == typeControls_.end() || control->second.active ? &metadata : nullptr;
        }
    return nullptr;
}

std::size_t ScriptEngine::GetFunctionMetadataCount() const {
    std::size_t count = 0;
    for (const auto& metadata : functionMetadata_) {
        bool host = false, active = false;
        for (const auto& candidate : hostFunctions_)
            if (candidate.signature.id == metadata.id) { host = true; active = active || candidate.active; }
        if (!host || active) ++count;
    }
    return count;
}

const FunctionMetadata* ScriptEngine::GetFunctionMetadataByIndex(std::size_t index) const {
    for (const auto& metadata : functionMetadata_) {
        bool host = false, active = false;
        for (const auto& candidate : hostFunctions_)
            if (candidate.signature.id == metadata.id) { host = true; active = active || candidate.active; }
        if (host && !active) continue;
        if (index-- == 0) return &metadata;
    }
    return nullptr;
}

const FunctionMetadata* ScriptEngine::GetFunctionMetadataById(FunctionId id) const {
    for (const auto& metadata : functionMetadata_)
        if (metadata.id == id) {
            bool host = false, active = false;
            for (const auto& candidate : hostFunctions_)
                if (candidate.signature.id == id) { host = true; active = active || candidate.active; }
            return !host || active ? &metadata : nullptr;
        }
    return nullptr;
}

std::size_t ScriptEngine::CollectGarbage() {
    const std::size_t collected = garbageCollector_.Collect();
    DrainFinalizers();
    return collected;
}

bool ScriptEngine::RegisterGlobalProperty(std::string declaration, Value* storage) {
    DiagnosticSink diagnostics([this](const Diagnostic& diagnostic) { ForwardDiagnostic(diagnostic); });
    auto signature = ParseGlobalPropertyDeclaration(declaration, diagnostics);
    if (!signature || !storage) {
        if (signature && !storage)
            diagnostics.Report({"registration"}, Severity::Error,
                               "global property storage cannot be null");
        return false;
    }
    signature->name = QualifyName(defaultNamespace_, std::move(signature->name));
    signature->type = ResolveRegisteredType(std::move(signature->type));
    if (!signature->type.IsNumeric() && signature->type != DataType::Bool() &&
        signature->type != DataType::String()) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "registered global properties currently support primitive and string values");
        return false;
    }
    if (storage->Type() != signature->type) {
        diagnostics.Report({"registration"}, Severity::Error,
                           "global property storage type does not match '" + signature->type.Name() + "'");
        return false;
    }
    for (const auto& existing : hostProperties_) {
        if (!existing.active) continue;
        if (existing.signature.name != signature->name) continue;
        diagnostics.Report({"registration"}, Severity::Error,
                           "duplicate global property '" + signature->name + "'");
        return false;
    }
    signature->id = GetOrCreateGlobalId("$host\n" + signature->name);
    hostProperties_.push_back({std::move(*signature), storage,
                               defaultAccessMask_, currentConfigGroup_, true});
    return true;
}
std::size_t ScriptEngine::GetTrackedObjectCount() const { return garbageCollector_.TrackedCount(); }

const TypeInfo* ScriptEngine::RegisterScriptType(const ClassSignature& signature) {
    TypeInfo* type = nullptr;
    const auto found = objectTypes_.find(signature.name);
    if (found == objectTypes_.end()) {
        auto created = std::make_unique<TypeInfo>();
        created->name = signature.name;
        created->id = signature.id.IsValid() ? signature.id : GetOrCreateTypeId(signature.name);
        type = created.get();
        objectTypes_.emplace(signature.name, std::move(created));
    } else type = found->second.get();
    type->script = !signature.interfaceType;
    type->host = false;
    type->valueType = false;
    type->defaultValue = Value{};
    type->baseClass = signature.baseClass;
    type->baseType = nullptr;
    type->collector = signature.interfaceType ? nullptr : &garbageCollector_;
    type->fields.clear();
    type->hostProperties.clear();
    type->fields.reserve(signature.fields.size());
    for (const auto& field : signature.fields)
        type->fields.emplace_back(field.name, field.type);
    type->interfaces = signature.interfaces;
    type->interfaceMethodTable.clear();
    for (const auto& interfaceName : signature.interfaces) {
        const auto interfaceFound = std::find_if(
            objectTypes_.begin(), objectTypes_.end(),
            [&](const auto& entry) { return entry.first == interfaceName; });
        (void)interfaceFound;
        for (const auto& method : signature.methods) {
            type->interfaceMethodTable[interfaceName + "::" + method.Declaration()] =
                signature.name + "::" + method.Declaration();
        }
    }
    return type;
}

void ScriptEngine::LinkScriptType(const ClassSignature& signature) {
    const auto found = objectTypes_.find(signature.name);
    if (found == objectTypes_.end()) return;
    TypeInfo* type = found->second.get();
    if (signature.baseClass.empty()) { type->baseType = nullptr; return; }
    const auto base = objectTypes_.find(signature.baseClass);
    type->baseType = base == objectTypes_.end() ? nullptr : base->second.get();
}

ScriptModule* ScriptEngine::GetModule(std::string name, ModulePolicy policy) {
    const auto found = modules_.find(name);
    if (policy == ModulePolicy::AlwaysCreate) {
        auto module = std::make_unique<ScriptModule>(*this, name);
        ScriptModule* result = module.get();
        modules_[std::move(name)] = std::move(module);
        return result;
    }
    if (found != modules_.end()) return found->second.get();
    if (policy == ModulePolicy::OnlyIfExists) return nullptr;
    auto module = std::make_unique<ScriptModule>(*this, name);
    ScriptModule* result = module.get();
    modules_.emplace(std::move(name), std::move(module));
    return result;
}

std::unique_ptr<ScriptContext> ScriptEngine::CreateContext() {
    return std::make_unique<ScriptContext>(*this);
}

void ScriptEngine::ForwardDiagnostic(const Diagnostic& diagnostic) const {
    if (messageCallback_) messageCallback_(diagnostic);
}

std::vector<FunctionSignature> ScriptEngine::HostSignatures(std::uint32_t accessMask) const {
    std::vector<FunctionSignature> signatures;
    signatures.reserve(hostFunctions_.size());
    for (const auto& host : hostFunctions_)
        if (host.active && !host.signature.method && IsVisible(host.accessMask, accessMask))
            signatures.push_back(host.signature);
    return signatures;
}

std::vector<GlobalSignature> ScriptEngine::HostPropertySignatures(std::uint32_t accessMask) const {
    std::vector<GlobalSignature> signatures;
    signatures.reserve(hostProperties_.size());
    for (const auto& host : hostProperties_)
        if (host.active && IsVisible(host.accessMask, accessMask))
            signatures.push_back(host.signature);
    return signatures;
}

std::vector<ClassSignature> ScriptEngine::HostTypeSignatures(std::uint32_t accessMask) const {
    std::vector<ClassSignature> signatures;
    for (const auto& entry : objectTypes_) {
        if (!entry.second->host || !entry.second->active ||
            entry.second->templateDefinition ||
            !IsVisible(entry.second->accessMask, accessMask)) continue;
        ClassSignature signature;
        signature.name = entry.second->name;
        signature.id = entry.second->id;
        signature.host = true;
        signature.valueType = entry.second->valueType;
        signature.defaultValue = entry.second->defaultValue;
        for (const auto* property : entry.second->hostProperties)
            if (property && property->active && IsVisible(property->accessMask, accessMask))
                signature.fields.push_back(property->signature);
        for (const auto& function : hostFunctions_)
            if (function.active && IsVisible(function.accessMask, accessMask) &&
                function.signature.method && function.signature.objectType == signature.name)
                signature.methods.push_back(function.signature);
        signatures.push_back(std::move(signature));
    }
    return signatures;
}

std::vector<std::pair<std::string, std::size_t>> ScriptEngine::HostTemplateTypes(
    std::uint32_t accessMask) const {
    std::vector<std::pair<std::string, std::size_t>> result;
    for (const auto& registered : templateTypes_) {
        if (!registered.definition || !registered.definition->active ||
            !IsVisible(registered.definition->accessMask, accessMask)) continue;
        result.emplace_back(registered.name, registered.parameters.size());
    }
    return result;
}

bool ScriptEngine::InstantiateTemplateTypes(const std::vector<TemplateTypeUse>& uses,
                                            std::uint32_t accessMask,
                                            DiagnosticSink& diagnostics) {
    for (const auto& use : uses) {
        const RegisteredTemplateType* registration = nullptr;
        for (const auto& candidate : templateTypes_) {
            if (candidate.name == use.templateName && candidate.definition &&
                candidate.definition->active &&
                IsVisible(candidate.definition->accessMask, accessMask)) {
                registration = &candidate;
                break;
            }
        }
        if (!registration) {
            diagnostics.Report(use.location, Severity::Error,
                               "template type '" + use.templateName + "' is unavailable");
            continue;
        }
        const auto existing = objectTypes_.find(use.instanceType.objectName);
        if (existing != objectTypes_.end() && existing->second->active) continue;
        if (registration->validator) {
            std::string reason;
            if (!registration->validator(use.subTypes, reason)) {
                diagnostics.Report(use.location, Severity::Error,
                    "template instance '" + use.instanceType.objectName +
                    "' is not supported" + (reason.empty() ? std::string{} : ": " + reason));
                continue;
            }
        }
        const std::string instanceName = use.instanceType.objectName;
        auto type = std::make_unique<TypeInfo>();
        type->name = instanceName;
        type->id = GetOrCreateTypeId(type->name);
        type->host = true;
        type->templateBase = registration->name;
        type->templateSubTypes = use.subTypes;
        type->accessMask = registration->definition->accessMask;
        type->configGroup = registration->definition->configGroup;
        if (registration->garbageCollected) type->collector = &garbageCollector_;
        TypeInfo* result = type.get();
        objectTypes_.insert_or_assign(instanceName, std::move(type));
        typeControls_[result->id.value] = {
            result->accessMask, result->configGroup, true};
        const std::size_t functionBegin = hostFunctions_.size();
        const std::size_t propertyBegin = hostObjectProperties_.size();
        if (registration->instanceCallback) {
            std::string reason;
            if (!registration->instanceCallback(*this, *result, reason)) {
                for (std::size_t index = functionBegin; index < hostFunctions_.size(); ++index)
                    hostFunctions_[index].active = false;
                for (std::size_t index = propertyBegin;
                     index < hostObjectProperties_.size(); ++index)
                    hostObjectProperties_[index].active = false;
                result->active = false;
                typeControls_[result->id.value].active = false;
                diagnostics.Report(use.location, Severity::Error,
                    "template instance '" + use.instanceType.objectName +
                    "' could not be configured" +
                    (reason.empty() ? std::string{} : ": " + reason));
                continue;
            }
        }
        for (std::size_t index = functionBegin; index < hostFunctions_.size(); ++index) {
            hostFunctions_[index].accessMask = result->accessMask;
            hostFunctions_[index].configGroup = result->configGroup;
        }
        for (std::size_t index = propertyBegin;
             index < hostObjectProperties_.size(); ++index) {
            hostObjectProperties_[index].accessMask = result->accessMask;
            hostObjectProperties_[index].configGroup = result->configGroup;
        }
        bool published = false;
        for (const auto& signature : HostTypeSignatures(~std::uint32_t{0})) {
            if (signature.name != result->name) continue;
            PublishObjectMetadata(signature);
            published = true;
            break;
        }
        if (!published) {
            ClassSignature signature;
            signature.name = result->name;
            signature.id = result->id;
            signature.host = true;
            PublishObjectMetadata(signature);
        }
    }
    return !diagnostics.HasErrors();
}

std::vector<EnumSignature> ScriptEngine::HostEnums(std::uint32_t accessMask) const {
    std::vector<EnumSignature> result;
    for (const auto& type : hostEnums_) {
        const auto control = typeControls_.find(type.id.value);
        if (control != typeControls_.end() && control->second.active &&
            IsVisible(control->second.accessMask, accessMask)) result.push_back(type);
    }
    return result;
}
std::vector<TypedefSignature> ScriptEngine::HostTypedefs(std::uint32_t accessMask) const {
    std::vector<TypedefSignature> result;
    for (const auto& type : hostTypedefs_) {
        const auto control = typeControls_.find(type.id.value);
        if (control != typeControls_.end() && control->second.active &&
            IsVisible(control->second.accessMask, accessMask)) result.push_back(type);
    }
    return result;
}
std::vector<FuncdefSignature> ScriptEngine::HostFuncdefs(std::uint32_t accessMask) const {
    std::vector<FuncdefSignature> result;
    for (const auto& type : hostFuncdefs_) {
        const auto control = typeControls_.find(type.id.value);
        if (control != typeControls_.end() && control->second.active &&
            IsVisible(control->second.accessMask, accessMask)) result.push_back(type);
    }
    return result;
}

void ScriptEngine::PublishTypeMetadata(TypeMetadata metadata) {
    for (auto& existing : typeMetadata_) {
        if (existing.id != metadata.id && existing.name != metadata.name) continue;
        existing = std::move(metadata);
        return;
    }
    typeMetadata_.push_back(std::move(metadata));
}

void ScriptEngine::PublishObjectMetadata(const ClassSignature& signature) {
    TypeMetadata metadata;
    metadata.id = signature.id;
    metadata.name = signature.name;
    metadata.kind = TypeMetadataKind::Object;
    metadata.host = signature.host;
    metadata.valueType = signature.valueType;
    metadata.interfaceType = signature.interfaceType;
    const auto registered = objectTypes_.find(signature.name);
    if (registered != objectTypes_.end()) {
        metadata.templateType = registered->second->templateDefinition;
        metadata.templateInstance = !registered->second->templateBase.empty() &&
                                    !registered->second->templateDefinition;
        metadata.templateBase = registered->second->templateBase;
        metadata.templateParameters = registered->second->templateParameters;
        metadata.templateSubTypes = registered->second->templateSubTypes;
    }
    metadata.baseClass = signature.baseClass;
    metadata.interfaces = signature.interfaces;
    metadata.fields = signature.fields;
    metadata.methods = signature.methods;
    PublishTypeMetadata(std::move(metadata));
}

void ScriptEngine::PublishEnumMetadata(const EnumSignature& signature, bool host) {
    TypeMetadata metadata;
    metadata.id = signature.id;
    metadata.name = signature.name;
    metadata.kind = TypeMetadataKind::Enum;
    metadata.host = host;
    metadata.enumValues = signature.values;
    PublishTypeMetadata(std::move(metadata));
}

void ScriptEngine::PublishTypedefMetadata(const TypedefSignature& signature, bool host) {
    TypeMetadata metadata;
    metadata.id = signature.id;
    metadata.name = signature.name;
    metadata.kind = TypeMetadataKind::Typedef;
    metadata.host = host;
    metadata.underlyingType = signature.underlyingType;
    PublishTypeMetadata(std::move(metadata));
}

void ScriptEngine::PublishFuncdefMetadata(const FuncdefSignature& signature, bool host) {
    TypeMetadata metadata;
    metadata.id = signature.id;
    metadata.name = signature.name;
    metadata.kind = TypeMetadataKind::Funcdef;
    metadata.host = host;
    metadata.funcdef = signature.signature;
    PublishTypeMetadata(std::move(metadata));
}

void ScriptEngine::PublishFunctionMetadata(FunctionSignature signature,
                                           std::string moduleName) {
    FunctionMetadata metadata{signature.id, std::move(moduleName), std::move(signature)};
    for (auto& existing : functionMetadata_) {
        if (existing.id != metadata.id) continue;
        existing = std::move(metadata);
        return;
    }
    functionMetadata_.push_back(std::move(metadata));
}

void ScriptEngine::PublishGlobalMetadata(GlobalSignature signature,
                                         std::string moduleName) {
    GlobalMetadata metadata{signature.id, std::move(moduleName), std::move(signature)};
    for (auto& existing : globalMetadata_) {
        if (existing.id != metadata.id) continue;
        existing = std::move(metadata);
        return;
    }
    globalMetadata_.push_back(std::move(metadata));
}

const GlobalMetadata* ScriptEngine::FindGlobalMetadata(GlobalId id) const {
    for (const auto& metadata : globalMetadata_)
        if (metadata.id == id) return &metadata;
    return nullptr;
}

DataType ScriptEngine::ResolveRegisteredType(DataType type) const {
    if (type.kind != TypeKind::Object) return type;
    const std::string requested = QualifyName(defaultNamespace_, type.objectName);
    for (const auto& alias : hostTypedefs_) {
        const auto control = typeControls_.find(alias.id.value);
        if (alias.name != requested || control == typeControls_.end() || !control->second.active) continue;
        if (type.isHandle) return DataType::Invalid();
        return alias.underlyingType;
    }
    for (const auto& typeInfo : hostEnums_) {
        const auto control = typeControls_.find(typeInfo.id.value);
        if (typeInfo.name != requested || control == typeControls_.end() || !control->second.active) continue;
        if (type.isHandle) return DataType::Invalid();
        return DataType::Enum(typeInfo.name);
    }
    for (const auto& typeInfo : hostFuncdefs_) {
        const auto control = typeControls_.find(typeInfo.id.value);
        if (typeInfo.name == requested && control != typeControls_.end() && control->second.active)
            return DataType::Function(typeInfo.name, type.isHandle);
    }
    const auto object = objectTypes_.find(requested);
    if (object != objectTypes_.end() && object->second->active)
        return DataType::Object(requested, type.isHandle);
    return type;
}

void ScriptEngine::ResolveRegisteredTypes(FunctionSignature& signature) const {
    signature.returnType = ResolveRegisteredType(std::move(signature.returnType));
    for (auto& parameter : signature.parameters)
        parameter = ResolveRegisteredType(std::move(parameter));
}

bool ScriptEngine::HasRegisteredType(std::string_view name) const {
    const auto object = objectTypes_.find(std::string(name));
    if (object != objectTypes_.end() && object->second->active) return true;
    for (const auto& type : templateTypes_)
        if (type.name == name && type.definition && type.definition->active) return true;
    const auto active = [&](TypeId id) {
        const auto control = typeControls_.find(id.value);
        return control != typeControls_.end() && control->second.active;
    };
    for (const auto& type : hostEnums_) if (type.name == name && active(type.id)) return true;
    for (const auto& type : hostTypedefs_) if (type.name == name && active(type.id)) return true;
    for (const auto& type : hostFuncdefs_) if (type.name == name && active(type.id)) return true;
    return false;
}

FunctionId ScriptEngine::GetOrCreateFunctionId(std::string key) {
    const auto found = functionIds_.find(key);
    if (found != functionIds_.end()) return found->second;
    const FunctionId id{nextFunctionId_++};
    functionIds_.emplace(std::move(key), id);
    return id;
}

TypeId ScriptEngine::GetOrCreateTypeId(std::string_view name) {
    const auto found = typeIds_.find(std::string(name));
    if (found != typeIds_.end()) return found->second;
    const TypeId id{nextTypeId_++};
    typeIds_.emplace(name, id);
    return id;
}

GlobalId ScriptEngine::GetOrCreateGlobalId(std::string key) {
    const auto found = globalIds_.find(key);
    if (found != globalIds_.end()) return found->second;
    const GlobalId id{nextGlobalId_++};
    globalIds_.emplace(std::move(key), id);
    return id;
}

void ScriptEngine::RegisterModuleImage(const std::shared_ptr<const ModuleImage>& image) {
    for (const auto& function : image->bytecode.functions) moduleImages_[&function] = image;
}

std::shared_ptr<const ModuleImage> ScriptEngine::FindModuleImage(const BytecodeFunction* function) {
    const auto found = moduleImages_.find(function);
    if (found == moduleImages_.end()) return {};
    auto image = found->second.lock();
    if (!image) moduleImages_.erase(found);
    return image;
}

void ScriptEngine::EnqueueFinalizer(ScriptObject* object) {
    if (object) finalizerQueue_.push_back(object);
}

void ScriptEngine::DrainFinalizers() {
    if (drainingFinalizers_) return;
    drainingFinalizers_ = true;
    while (!finalizerQueue_.empty()) {
        ScriptObject* object = finalizerQueue_.front();
        finalizerQueue_.pop_front();
        const ScriptFinalizerBinding binding = object->Finalizer();
        auto state = binding.state.lock();
        for (const FunctionId functionId : binding.functions) {
            const BytecodeFunction* function = binding.module
                ? binding.module->FindFunction(functionId) : nullptr;
            if (!function) {
                ForwardDiagnostic({{"finalizer"}, Severity::Error,
                                   "script destructor target is unavailable"});
                continue;
            }
            VirtualMachine finalizer;
            finalizer.SetFinalizerContext(this, binding.module, binding.state,
                                          [this] { DrainFinalizers(); });
            const ExecutionResult result = finalizer.Execute(
                *function, {Value(ObjectHandle(object))}, binding.module.get(), state.get());
            if (result.state != ExecutionState::Finished) {
                ForwardDiagnostic({result.location, Severity::Error,
                                   "script destructor '" + function->signature.name +
                                   "' failed: " + result.exception});
            }
        }
        object->Release();
    }
    drainingFinalizers_ = false;
}

std::unique_ptr<ScriptEngine> CreateScriptEngine() { return std::make_unique<ScriptEngine>(); }

} // namespace mini_as
