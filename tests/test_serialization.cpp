#include "test.hpp"
#include "mini_as/addons/any.hpp"
#include "mini_as/addons/array.hpp"
#include "mini_as/addons/dictionary.hpp"
#include "mini_as/addons/ref.hpp"
#include "mini_as/compat.hpp"
#include "mini_as/engine.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace {

mini_as::ExecutionState Run(mini_as::ScriptEngine& engine,
                            mini_as::ScriptModule& module,
                            std::string_view declaration,
                            mini_as::Value* result = nullptr) {
    auto context = engine.CreateContext();
    if (!context->Prepare(module.GetFunctionByDecl(declaration)))
        return mini_as::ExecutionState::Exception;
    const auto state = context->Execute();
    if (result && state == mini_as::ExecutionState::Finished)
        *result = context->GetReturnValue();
    return state;
}

class HostPayload final : public mini_as::RefObject {
public:
    explicit HostPayload(const mini_as::TypeInfo* type) : RefObject(type) {}

private:
    ~HostPayload() override = default;
};

} // namespace

TEST_CASE(module_state_round_trips_globals_cycles_weakrefs_and_closures) {
    const std::string source =
        "funcdef int Transform(int);\n"
        "class Node { string name; Node@ next; }\n"
        "int counter = 1; string label = \"initial\";\n"
        "Node@ root; Node@ alias; weakref<Node> observer; Transform@ transform;\n"
        "void seed() {\n"
        "  counter = 40; label = \"saved\";\n"
        "  @root = Node(); root.name = \"root\";\n"
        "  Node@ child = Node(); child.name = \"child\";\n"
        "  root.next = child; child.next = root; @alias = child; @observer = root;\n"
        "  int captured = 2; @transform = function(value) { return value + captured; };\n"
        "}\n"
        "int verify() {\n"
        "  Node@ watched = observer.get();\n"
        "  bool graph = !(root is null) && !(alias is null) && watched is root &&\n"
        "    root.next is alias && alias.next is root && root.name == \"root\" &&\n"
        "    alias.name == \"child\" && label == \"saved\";\n"
        "  return graph ? transform(counter) : -1;\n"
        "}\n";

    std::stringstream bytecode(std::ios::in | std::ios::out | std::ios::binary);
    std::stringstream state(std::ios::in | std::ios::out | std::ios::binary);
    {
        auto engine = mini_as::CreateScriptEngine();
        std::vector<mini_as::Diagnostic> diagnostics;
        engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
            diagnostics.push_back(diagnostic);
        });
        auto* module = engine->GetModule("state-graph");
        module->AddScriptSection("state-graph.as", source);
        if (!module->Build()) {
            if (diagnostics.empty()) throw std::runtime_error("state graph build failed");
            const auto& diagnostic = diagnostics.back();
            throw std::runtime_error(std::to_string(diagnostic.location.row) + ":" +
                std::to_string(diagnostic.location.column) + ":" + diagnostic.message);
        }
        CHECK(Run(*engine, *module, "void seed()") == mini_as::ExecutionState::Finished);
        mini_as::Value result;
        CHECK(Run(*engine, *module, "int verify()", &result) ==
              mini_as::ExecutionState::Finished);
        CHECK(result.As<std::int32_t>() == 42);
        CHECK(module->SaveBytecode(bytecode));
        CHECK(module->SaveState(state));
    }

    bytecode.seekg(0);
    state.seekg(0);
    auto restoredEngine = mini_as::CreateScriptEngine();
    auto* restored = restoredEngine->GetModule("state-graph");
    CHECK(restored->LoadBytecode(bytecode));
    CHECK(restored->LoadState(state));
    mini_as::Value result;
    CHECK(Run(*restoredEngine, *restored, "int verify()", &result) ==
          mini_as::ExecutionState::Finished);
    CHECK(result.As<std::int32_t>() == 42);
    CHECK(restoredEngine->GetTrackedObjectCount() == 2);
}

TEST_CASE(module_state_round_trips_standard_addon_objects) {
    const std::string source =
        "class Node { int value; Node@ next; }\n"
        "array<Node@>@ nodes; dictionary@ values; any@ extra;\n"
        "void seed() {\n"
        "  @nodes = array<Node@>(); Node@ a = Node(); Node@ b = Node();\n"
        "  a.value = 20; b.value = 20; @a.next = b; @b.next = a;\n"
        "  nodes.insertLast(a); nodes.insertLast(b);\n"
        "  @values = dictionary(); values.set(\"answer\", int64(1));\n"
        "  @extra = any(int64(1));\n"
        "}\n"
        "int verify() {\n"
        "  int64 first = 0; int64 second = 0;\n"
        "  bool ok = values.get(\"answer\", first) && extra.retrieve(second);\n"
        "  return ok && nodes.length() == 2 && nodes[0].next is nodes[1] &&\n"
        "    nodes[1].next is nodes[0] ? nodes[0].value + nodes[1].value + first + second : 0;\n"
        "}\n";
    std::stringstream bytecode(std::ios::in | std::ios::out | std::ios::binary);
    std::stringstream state(std::ios::in | std::ios::out | std::ios::binary);
    {
        auto engine = mini_as::CreateScriptEngine();
        CHECK(mini_as::addons::RegisterScriptArray(*engine));
        CHECK(mini_as::addons::RegisterScriptRef(*engine));
        CHECK(mini_as::addons::RegisterScriptAny(*engine));
        CHECK(mini_as::addons::RegisterScriptDictionary(*engine));
        auto* module = engine->GetModule("addon-state");
        module->AddScriptSection("addon-state.as", source);
        CHECK(module->Build());
        CHECK(Run(*engine, *module, "void seed()") == mini_as::ExecutionState::Finished);
        CHECK(module->SaveBytecode(bytecode));
        CHECK(module->SaveState(state));
    }

    bytecode.seekg(0);
    state.seekg(0);
    auto engine = mini_as::CreateScriptEngine();
    CHECK(mini_as::addons::RegisterScriptArray(*engine));
    CHECK(mini_as::addons::RegisterScriptRef(*engine));
    CHECK(mini_as::addons::RegisterScriptAny(*engine));
    CHECK(mini_as::addons::RegisterScriptDictionary(*engine));
    auto* module = engine->GetModule("addon-state");
    CHECK(module->LoadBytecode(bytecode));
    CHECK(module->LoadState(state));
    mini_as::Value result;
    CHECK(Run(*engine, *module, "int verify()", &result) ==
          mini_as::ExecutionState::Finished);
    CHECK(result.As<std::int32_t>() == 42);
}

TEST_CASE(module_state_load_is_transactional_for_corrupt_archives) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("transactional-state");
    module->AddScriptSection("transactional.as",
        "int value = 1; void set(int next) { value = next; } int get() { return value; }");
    CHECK(module->Build());
    auto set = [&](std::int32_t value) {
        auto context = engine->CreateContext();
        CHECK(context->Prepare(module->GetFunctionByDecl("void set(int)")));
        CHECK(context->SetArgInt(0, value));
        CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    };
    set(42);
    std::stringstream saved(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveState(saved));
    std::string corrupted = saved.str();
    CHECK(corrupted.size() > 24);
    corrupted.back() = static_cast<char>(corrupted.back() ^ 0x5a);
    set(99);
    std::stringstream input(corrupted, std::ios::in | std::ios::binary);
    CHECK(!module->LoadState(input));
    mini_as::Value result;
    CHECK(Run(*engine, *module, "int get()", &result) ==
          mini_as::ExecutionState::Finished);
    CHECK(result.As<std::int32_t>() == 99);
    bool checksum = false;
    for (const auto& diagnostic : diagnostics)
        checksum = checksum || diagnostic.message.find("checksum mismatch") != std::string::npos;
    CHECK(checksum);
}

TEST_CASE(module_state_rejects_unregistered_host_object_codecs) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    const auto* type = engine->RegisterObjectType("HostPayload");
    CHECK(type != nullptr);
    CHECK(engine->RegisterObjectFactory("HostPayload", "HostPayload@ f()",
        [type](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(new HostPayload(type)));
        }));
    auto* module = engine->GetModule("unsupported-state-object");
    module->AddScriptSection("unsupported.as", "HostPayload@ value = HostPayload();");
    CHECK(module->Build());
    std::stringstream state(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(!module->SaveState(state));
    bool codec = false;
    for (const auto& diagnostic : diagnostics)
        codec = codec || diagnostic.message.find("no state serialization codec") !=
            std::string::npos;
    CHECK(codec);
}

TEST_CASE(module_state_does_not_capture_registered_global_properties) {
    auto engine = mini_as::CreateScriptEngine();
    mini_as::Value hostValue(std::int32_t{40});
    CHECK(engine->RegisterGlobalProperty("int hostValue", &hostValue));
    auto* module = engine->GetModule("external-property-state");
    module->AddScriptSection("external-property.as",
        "int localValue = 1; void setLocal(int value) { localValue = value; } "
        "int inspect() { return hostValue + localValue; }");
    CHECK(module->Build());
    auto setLocal = [&](std::int32_t value) {
        auto context = engine->CreateContext();
        CHECK(context->Prepare(module->GetFunctionByDecl("void setLocal(int)")));
        CHECK(context->SetArgInt(0, value));
        CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    };
    setLocal(2);
    std::stringstream state(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveState(state));
    setLocal(100);
    hostValue = mini_as::Value(std::int32_t{10});
    state.seekg(0);
    CHECK(module->LoadState(state));
    mini_as::Value result;
    CHECK(Run(*engine, *module, "int inspect()", &result) ==
          mini_as::ExecutionState::Finished);
    CHECK(result.As<std::int32_t>() == 12);
}

TEST_CASE(restored_script_objects_run_their_destructor_exactly_once) {
    auto engine = mini_as::CreateScriptEngine();
    std::size_t destructions = 0;
    CHECK(engine->RegisterGlobalFunction("void noteDestroyed()",
        [&](mini_as::GenericCall&) { ++destructions; }));
    auto* module = engine->GetModule("serialized-finalizer");
    module->AddScriptSection("serialized-finalizer.as",
        "class Payload { ~Payload() { noteDestroyed(); } } Payload@ value; "
        "void seed() { @value = Payload(); } void clear() { @value = null; }");
    CHECK(module->Build());
    CHECK(Run(*engine, *module, "void seed()") == mini_as::ExecutionState::Finished);
    std::stringstream state(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveState(state));
    CHECK(Run(*engine, *module, "void clear()") == mini_as::ExecutionState::Finished);
    CHECK(destructions == 1);
    state.seekg(0);
    CHECK(module->LoadState(state));
    CHECK(destructions == 1);
    CHECK(Run(*engine, *module, "void clear()") == mini_as::ExecutionState::Finished);
    CHECK(destructions == 2);
    engine->CollectGarbage();
    CHECK(destructions == 2);
}

TEST_CASE(module_state_rejects_a_different_global_schema_without_mutation) {
    std::stringstream state(std::ios::in | std::ios::out | std::ios::binary);
    {
        auto sourceEngine = mini_as::CreateScriptEngine();
        auto* source = sourceEngine->GetModule("schema-state");
        source->AddScriptSection("source.as", "int value = 42;");
        CHECK(source->Build());
        CHECK(source->SaveState(state));
    }
    state.seekg(0);
    auto targetEngine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    targetEngine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* target = targetEngine->GetModule("schema-state");
    target->AddScriptSection("target.as",
        "string value = \"preserved\"; string inspect() { return value; }");
    CHECK(target->Build());
    CHECK(!target->LoadState(state));
    mini_as::Value result;
    CHECK(Run(*targetEngine, *target, "string inspect()", &result) ==
          mini_as::ExecutionState::Finished);
    CHECK(result.As<std::string>() == "preserved");
    bool schema = false;
    for (const auto& diagnostic : diagnostics)
        schema = schema || diagnostic.message.find("global schema does not match") !=
            std::string::npos;
    CHECK(schema);
}

TEST_CASE(compat_facade_saves_and_loads_live_module_state) {
    using namespace mini_as::compat;
    auto engine = CreateScriptEngine();
    auto* module = engine->GetModule("compat-state", asGM_ALWAYS_CREATE);
    CHECK(module->AddScriptSection("compat-state.as",
        "int value = 40; int next() { return ++value; }") == asSUCCESS);
    CHECK(module->Build() == asSUCCESS);
    std::stringstream state(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveState(state) == asSUCCESS);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int next()")) == asSUCCESS);
    CHECK(context->Execute() == asEXECUTION_FINISHED);
    CHECK(context->GetReturnDWord() == 41);
    state.seekg(0);
    CHECK(module->LoadState(state) == asSUCCESS);
    CHECK(context->Prepare(module->GetFunctionByDecl("int next()")) == asSUCCESS);
    CHECK(context->Execute() == asEXECUTION_FINISHED);
    CHECK(context->GetReturnDWord() == 41);
}
