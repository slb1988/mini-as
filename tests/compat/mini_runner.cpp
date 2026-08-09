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
    const auto* hostRefType = engine->RegisterObjectType("HostRef");
    if (!hostRefType) return 4;
    if (!engine->RegisterObjectProperty("HostRef", "int value",
        [](const mini_as::ObjectHandle& object) {
            const auto* value = dynamic_cast<const CompatReference*>(object.Get());
            return mini_as::Value(std::int32_t{value ? value->value : 0});
        },
        [](const mini_as::ObjectHandle& object, mini_as::Value value) {
            auto* target = dynamic_cast<CompatReference*>(object.Get());
            if (!target) throw std::runtime_error("invalid HostRef receiver");
            target->value = value.As<std::int32_t>();
        })) return 5;
    if (!engine->RegisterObjectFactory("HostRef", "HostRef@ f(int value)",
        [hostRefType](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(
                new CompatReference(hostRefType, call.GetArgInt(0))));
        })) return 6;
    if (!engine->RegisterObjectMethod("HostRef", "int get() const",
        [](mini_as::GenericCall& call) {
            const auto* value = dynamic_cast<const CompatReference*>(call.GetObject().Get());
            if (!value) { call.SetException("invalid HostRef receiver"); return; }
            call.SetReturnInt(value->value);
        })) return 7;
    auto* module = engine->GetModule("compat", mini_as::ModulePolicy::AlwaysCreate);
    module->AddScriptSection("compat.as", ReadFile(argv[1]));
    if (!module->Build()) return 8;
    auto context = engine->CreateContext();
    if (!context->Prepare(module->GetFunctionByDecl("int main()"))) return 9;
    if (context->Execute() != mini_as::ExecutionState::Finished) return 10;
    std::cout << "state=finished\nreturn=int:" << context->GetReturnInt() << '\n';
    return 0;
}
