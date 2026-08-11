#include "mini_as/engine.hpp"
#include "mini_as/compat.hpp"
#include "mini_as/addons/any.hpp"
#include "mini_as/addons/array.hpp"
#include "mini_as/addons/dictionary.hpp"
#include "mini_as/addons/ref.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

class CompatReference final : public mini_as::RefObject {
public:
    CompatReference(const mini_as::TypeInfo* type, int value)
        : RefObject(type), value(value) {}
    int value;

private:
    ~CompatReference() override = default;
};

struct CompatValue {
    int value = 42;
};

std::string ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream source;
    source << input.rdbuf();
    return source.str();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (std::string(argv[1]).find("compat_facade") != std::string::npos) {
        auto facade = mini_as::compat::CreateScriptEngine();
        auto* facadeModule = facade->GetModule("compat", mini_as::compat::asGM_ALWAYS_CREATE);
        const std::string source = ReadFile(argv[1]);
        if (!facadeModule || facadeModule->AddScriptSection(
                "compat.as", source.c_str(), source.size()) != mini_as::compat::asSUCCESS ||
            facadeModule->Build() != mini_as::compat::asSUCCESS) return 3;
        auto facadeContext = facade->CreateContext();
        if (facadeContext->Prepare(facadeModule->GetFunctionByDecl("int main()")) !=
                mini_as::compat::asSUCCESS ||
            facadeContext->Execute() != mini_as::compat::asEXECUTION_FINISHED) return 4;
        std::cout << "state=finished\nreturn=int:" << facadeContext->GetReturnDWord() << '\n';
        return 0;
    }
    auto engine = mini_as::CreateScriptEngine();
    const bool anyRefObjects =
        std::string(argv[1]).find("any_ref_objects") != std::string::npos;
    if (anyRefObjects && (!mini_as::addons::RegisterScriptRef(*engine) ||
                          !mini_as::addons::RegisterScriptAny(*engine))) return 3;
    const bool hostControls = std::string(argv[1]).find("host_controls") != std::string::npos;
    const bool registeredTemplates =
        std::string(argv[1]).find("registered_template_types") != std::string::npos;
    const bool arrayTemplate =
        std::string(argv[1]).find("array_template_object") != std::string::npos ||
        std::string(argv[1]).find("initialization_lists") != std::string::npos ||
        std::string(argv[1]).find("indexing_expressions") != std::string::npos ||
        std::string(argv[1]).find("foreach_operator_protocol") != std::string::npos ||
        std::string(argv[1]).find("dictionary_object") != std::string::npos;
    if (arrayTemplate && !mini_as::addons::RegisterScriptArray(*engine)) return 3;
    if (std::string(argv[1]).find("dictionary_object") != std::string::npos &&
        !mini_as::addons::RegisterScriptDictionary(*engine)) return 3;
    if (registeredTemplates &&
        !engine->RegisterObjectType("HostBox<class T>")) return 3;
    if (hostControls) {
        if (!engine->SetDefaultNamespace("HostTools") ||
            engine->SetDefaultAccessMask(0x1u) != ~std::uint32_t{0} ||
            !engine->BeginConfigGroup("runtime") ||
            !engine->RegisterGlobalFunction("int Scoped(int value)",
                [](mini_as::GenericCall& call) { call.SetReturnInt(call.GetArgInt(0) + 2); }) ||
            !engine->EndConfigGroup()) return 3;
        engine->SetDefaultAccessMask(0x2u);
        if (!engine->RegisterGlobalFunction("int Hidden()",
                [](mini_as::GenericCall& call) { call.SetReturnInt(99); }) ||
            !engine->SetDefaultNamespace("")) return 3;
        engine->SetDefaultAccessMask(~std::uint32_t{0});
    }
    if (std::string(argv[1]).find("variadic_arguments") != std::string::npos &&
        !engine->RegisterGlobalFunction(
            "int VariadicSum(int seed, const int &in ...)",
            [](mini_as::GenericCall& call) {
                int total = 0;
                for (std::size_t index = 0; index < call.GetArgCount(); ++index)
                    total += call.GetArgInt(index);
                call.SetReturnInt(total);
            })) return 3;
    mini_as::Value hostCounter(std::int32_t{40});
    engine->SetMessageCallback([](const mini_as::Diagnostic& diagnostic) {
        std::cerr << diagnostic.location.row << ':' << diagnostic.location.column
                  << ':' << diagnostic.message << '\n';
    });
    if (!engine->RegisterGlobalProperty("int hostCounter", &hostCounter)) return 3;
    if (!engine->RegisterEnum("HostColor") ||
        !engine->RegisterEnumValue("HostColor", "HostRed", 40) ||
        !engine->RegisterEnumValue("HostColor", "HostBlue", 42) ||
        !engine->RegisterTypedef("HostScore", mini_as::DataType::Int()) ||
        !engine->RegisterFuncdef("HostScore HostTransform(HostScore value)")) return 4;
    if (!engine->RegisterGlobalFunction("HostScore Lift(HostScore value)",
        [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetArgInt(0) + 2);
        })) return 5;
    const auto* reflectedEnum = engine->GetTypeMetadataByName("HostColor");
    const auto* reflectedAlias = engine->GetTypeMetadataByName("HostScore");
    const auto* reflectedFuncdef = engine->GetTypeMetadataByName("HostTransform");
    const mini_as::FunctionMetadata* reflectedLift = nullptr;
    for (std::size_t index = 0; index < engine->GetFunctionMetadataCount(); ++index) {
        const auto* function = engine->GetFunctionMetadataByIndex(index);
        if (function && function->signature.name == "Lift") reflectedLift = function;
    }
    if (!reflectedEnum || reflectedEnum->enumValues.size() != 2 ||
        !reflectedAlias || reflectedAlias->underlyingType != mini_as::DataType::Int() ||
        !reflectedFuncdef || reflectedFuncdef->funcdef.parameters.size() != 1 ||
        !reflectedLift || !reflectedLift->signature.host ||
        reflectedLift->signature.parameters.size() != 1) return 5;
    if (!engine->RegisterValueType("HostValue",
        mini_as::Value::HostValue("HostValue", CompatValue{}))) return 6;
    if (!engine->RegisterGlobalFunction("int ReadHostValue(HostValue value)",
        [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetArg(0).AsHostValue<CompatValue>().value);
        })) return 7;
    if (!engine->RegisterObjectMethod("HostValue", "int get() const",
        [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetObjectValue().AsHostValue<CompatValue>().value);
        })) return 8;
    const auto* hostRefType = engine->RegisterObjectType("HostRef");
    if (!hostRefType) return 9;
    if (!engine->RegisterObjectProperty("HostRef", "int value",
        [](const mini_as::ObjectHandle& object) {
            const auto* value = dynamic_cast<const CompatReference*>(object.Get());
            return mini_as::Value(std::int32_t{value ? value->value : 0});
        },
        [](const mini_as::ObjectHandle& object, mini_as::Value value) {
            auto* target = dynamic_cast<CompatReference*>(object.Get());
            if (!target) throw std::runtime_error("invalid HostRef receiver");
            target->value = value.As<std::int32_t>();
        })) return 10;
    if (!engine->RegisterObjectFactory("HostRef", "HostRef@ f(int value)",
        [hostRefType](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(
                new CompatReference(hostRefType, call.GetArgInt(0))));
        })) return 11;
    if (!engine->RegisterObjectMethod("HostRef", "int get() const",
        [](mini_as::GenericCall& call) {
            const auto* value = dynamic_cast<const CompatReference*>(call.GetObject().Get());
            if (!value) { call.SetException("invalid HostRef receiver"); return; }
            call.SetReturnInt(value->value);
        })) return 12;
    const bool importedFunctions =
        std::string(argv[1]).find("imported_functions") != std::string::npos;
    const bool sharedEntities =
        std::string(argv[1]).find("shared_entities") != std::string::npos;
    const bool externalEntities =
        std::string(argv[1]).find("external_entities") != std::string::npos;
    if (importedFunctions || sharedEntities || externalEntities) {
        auto* source = engine->GetModule(importedFunctions ? "math" : "shared-source",
                                         mini_as::ModulePolicy::AlwaysCreate);
        source->AddScriptSection("source.as", importedFunctions
            ? "int total = 40; int Add(int value) { total += value; return total; }"
            : "shared interface ICounter { int read(); } "
              "shared class Counter : ICounter { int value; Counter(int start) { value = start; } "
              "int read() { return value; } } "
              "shared int Twice(int value) { return value * 2; } "
              "Counter@ Make() { return Counter(40); }");
        if (!source->Build()) return 13;
    }
    auto* module = engine->GetModule("compat", mini_as::ModulePolicy::AlwaysCreate);
    if (hostControls) module->SetAccessMask(0x1u);
    module->AddScriptSection("compat.as", ReadFile(argv[1]));
    if (!module->Build()) return 13;
    if ((importedFunctions || sharedEntities) &&
        !module->BindAllImportedFunctions()) return 13;
    if (hostControls && engine->RemoveConfigGroup("runtime")) return 13;
    if (std::string(argv[1]).find("registered_named_types") != std::string::npos) {
        const auto* selected = module->GetGlobalMetadataByDecl("HostColor selected");
        const auto* transform = module->GetGlobalMetadataByName("transform");
        if (module->GetGlobalMetadataCount() != 2 || !selected || !transform ||
            transform->signature.Declaration() != "HostTransform@ transform") return 13;
        const auto* detached = module->CompileFunction(
            "dynamic-detached", "int detached_probe() { return main(); }", false);
        const auto* added = module->CompileFunction(
            "dynamic-added", "int dynamic_probe() { return main(); }");
        if (!detached || module->GetFunctionByDecl("int detached_probe()") || !added ||
            module->GetFunctionByDecl("int dynamic_probe()") != added) return 13;
        const auto* caller = module->CompileFunction(
            "dynamic-caller", "int dynamic_caller() { return dynamic_probe(); }");
        if (!caller) return 13;
        std::stringstream bytecode(std::ios::in | std::ios::out | std::ios::binary);
        if (!module->SaveBytecode(bytecode)) return 13;
        bytecode.seekg(0);
        auto* loaded = engine->GetModule("compat-loaded", mini_as::ModulePolicy::AlwaysCreate);
        if (!loaded->LoadBytecode(bytecode) ||
            !loaded->GetFunctionByDecl("int main()") ||
            !loaded->GetFunctionByDecl("int dynamic_caller()") ||
            !loaded->GetFunctionByDecl("int dynamic_probe()") ||
            !module->RemoveFunction(added) ||
            module->GetFunctionByDecl("int dynamic_probe()") ||
            !module->GetFunctionByDecl("int dynamic_caller()")) return 13;
    }
    auto context = engine->CreateContext();
    if (!context->Prepare(module->GetFunctionByDecl("int main()"))) return 14;
    const bool debugIntrospection =
        std::string(argv[1]).find("debug_introspection") != std::string::npos;
    bool debugPrinted = false;
    if (debugIntrospection) {
        context->SetLineCallback([&](mini_as::ScriptContext& current,
                                     const mini_as::SourceLocation& location) {
            if (debugPrinted || location.row != 3) return;
            debugPrinted = true;
            const auto* function = current.GetFunction(0);
            const auto* caller = current.GetFunction(1);
            std::cout << "debug=" << (function ? function->signature.name : "<null>")
                      << ':' << current.GetInstructionLocation(0).row
                      << ':' << current.GetCallStackSize() << '\n';
            for (const auto& local : current.GetLocals(0))
                if (local.inScope && (local.name == "value" || local.name == "doubled"))
                    std::cout << "local=" << local.name << ':'
                              << local.value.As<std::int32_t>() << '\n';
            std::cout << "caller=" << (caller ? caller->signature.name : "<null>")
                      << ':' << current.GetInstructionLocation(1).row << '\n';
            for (const auto& local : current.GetLocals(1))
                if (local.inScope && local.name == "seed")
                    std::cout << "caller-local=" << local.name << ':'
                              << local.value.As<std::int32_t>() << '\n';
        });
    }
    if (context->Execute() != mini_as::ExecutionState::Finished) return 15;
    if (debugIntrospection && !debugPrinted) return 15;
    std::cout << "state=finished\nreturn=int:" << context->GetReturnInt() << '\n';
    return 0;
}
