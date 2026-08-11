#include "mini_as/engine.hpp"

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
    auto engine = mini_as::CreateScriptEngine();
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
    auto* module = engine->GetModule("compat", mini_as::ModulePolicy::AlwaysCreate);
    module->AddScriptSection("compat.as", ReadFile(argv[1]));
    if (!module->Build()) return 13;
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
    }
    auto context = engine->CreateContext();
    if (!context->Prepare(module->GetFunctionByDecl("int main()"))) return 14;
    if (context->Execute() != mini_as::ExecutionState::Finished) return 15;
    std::cout << "state=finished\nreturn=int:" << context->GetReturnInt() << '\n';
    return 0;
}
