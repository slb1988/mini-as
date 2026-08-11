#include "test.hpp"
#include "mini_as/compat.hpp"

#include <cstdint>
#include <sstream>

TEST_CASE(compat_constants_match_angelscript_238_values) {
    using namespace mini_as::compat;
    CHECK(asSUCCESS == 0);
    CHECK(asINVALID_ARG == -5);
    CHECK(asNO_FUNCTION == -6);
    CHECK(asINVALID_DECLARATION == -10);
    CHECK(asMODULE_IS_IN_USE == -28);
    CHECK(asEXECUTION_FINISHED == 0);
    CHECK(asEXECUTION_SUSPENDED == 1);
    CHECK(asEXECUTION_EXCEPTION == 3);
    CHECK(asEXECUTION_ACTIVE == 6);
    CHECK(asGM_ONLY_IF_EXISTS == 0);
    CHECK(asGM_ALWAYS_CREATE == 2);
    CHECK(asCOMP_ADD_TO_MODULE == 1);
}

TEST_CASE(compat_facade_builds_registers_and_executes_with_integer_codes) {
    using namespace mini_as::compat;
    auto engine = CreateScriptEngine();
    CHECK(engine->RegisterGlobalFunction("int AddHost(int value)",
        [](mini_as::GenericCall& call) { call.SetReturnInt(call.GetArgInt(0) + 2); }) == asSUCCESS);
    CHECK(engine->RegisterGlobalFunction(nullptr, {}) == asINVALID_DECLARATION);
    CHECK(engine->RegisterGlobalFunction("int Missing()", {}) == asINVALID_ARG);
    CHECK(engine->GetModule("facade", asGM_ONLY_IF_EXISTS) == nullptr);
    auto* module = engine->GetModule("facade", asGM_CREATE_IF_NOT_EXISTS);
    CHECK(module != nullptr);
    CHECK(engine->GetModule("facade", asGM_CREATE_IF_NOT_EXISTS) == module);
    CHECK(module->AddScriptSection(nullptr, "int main() { return 0; }") == asINVALID_NAME);
    CHECK(module->AddScriptSection("facade.as",
        "int twice(int value) { return AddHost(value) * 2; }", 0, 2) == asSUCCESS);
    CHECK(module->Build() == asSUCCESS);
    const auto* function = module->GetFunctionByDecl("int twice(int)");
    CHECK(function != nullptr);
    CHECK(!function->code.empty());
    CHECK(function->code.front().location.row == 3);

    auto context = engine->CreateContext();
    CHECK(context->Execute() == asCONTEXT_NOT_PREPARED);
    CHECK(context->Prepare(nullptr) == asNO_FUNCTION);
    CHECK(context->Prepare(function) == asSUCCESS);
    CHECK(context->GetState() == asEXECUTION_PREPARED);
    CHECK(context->SetArgDWord(1, 20) == asINVALID_ARG);
    CHECK(context->SetArgDWord(0, 20) == asSUCCESS);
    CHECK(context->Execute() == asEXECUTION_FINISHED);
    CHECK(context->GetReturnDWord() == 44);
    CHECK(context->Execute() == asCONTEXT_NOT_PREPARED);
}

TEST_CASE(compat_facade_supports_dynamic_functions_bytecode_and_debug_queries) {
    using namespace mini_as::compat;
    auto engine = CreateScriptEngine();
    auto* module = engine->GetModule("facade-debug", asGM_ALWAYS_CREATE);
    CHECK(module->AddScriptSection("facade-debug.as",
        "int leaf(int input) {\n"
        "  int local = input + 1;\n"
        "  return local;\n"
        "}\n"
        "int main() { return leaf(41); }\n") == asSUCCESS);
    CHECK(module->Build() == asSUCCESS);
    const mini_as::BytecodeFunction* dynamic = nullptr;
    CHECK(module->CompileFunction("dynamic", "int probe() { return main(); }", 0,
                                  asCOMP_ADD_TO_MODULE, &dynamic) == asSUCCESS);
    CHECK(dynamic != nullptr);

    auto context = engine->CreateContext();
    CHECK(context->Prepare(dynamic) == asSUCCESS);
    bool inspected = false;
    context->SetLineCallback([&](mini_as::compat::ScriptContext& current,
                                 const mini_as::SourceLocation& location) {
        if (inspected || location.row != 3) return;
        inspected = true;
        CHECK(&current == context.get());
        CHECK(current.GetState() == asEXECUTION_ACTIVE);
        CHECK(current.GetCallstackSize() == 3);
        CHECK(current.GetLineNumber(0) == 3);
        CHECK(current.GetVarCount(0) == 2);
        const char* name = nullptr;
        mini_as::DataType type;
        bool inScope = false;
        const mini_as::Value* value = nullptr;
        CHECK(current.GetVar(1, 0, &name, &type, &inScope, &value) == asSUCCESS);
        CHECK(std::string(name) == "local");
        CHECK(type == mini_as::DataType::Int());
        CHECK(inScope);
        CHECK(value->As<std::int32_t>() == 42);
        CHECK(current.GetVar(99, 0, nullptr) == asINVALID_ARG);
    });
    CHECK(context->Execute() == asEXECUTION_FINISHED);
    CHECK(inspected);
    CHECK(context->GetReturnDWord() == 42);

    std::stringstream archive(std::ios::in | std::ios::out | std::ios::binary);
    CHECK(module->SaveByteCode(archive) == asSUCCESS);
    auto targetEngine = CreateScriptEngine();
    auto* target = targetEngine->GetModule("facade-debug-loaded", asGM_ALWAYS_CREATE);
    archive.seekg(0);
    CHECK(target->LoadByteCode(archive) == asSUCCESS);
    CHECK(target->GetFunctionByDecl("int probe()") != nullptr);
    CHECK(module->RemoveFunction(dynamic) == asSUCCESS);
    CHECK(module->RemoveFunction(dynamic) == asNO_FUNCTION);
}

TEST_CASE(host_namespaces_access_masks_and_configuration_groups_control_modules) {
    using namespace mini_as::compat;
    auto engine = CreateScriptEngine();
    CHECK(engine->SetDefaultNamespace("HostTools") == asSUCCESS);
    CHECK(std::string(engine->GetDefaultNamespace()) == "HostTools");
    CHECK(engine->SetDefaultAccessMask(0x1u) == ~std::uint32_t{0});
    CHECK(engine->BeginConfigGroup("runtime") == asSUCCESS);
    CHECK(engine->BeginConfigGroup("nested") == asINVALID_ARG);
    CHECK(engine->RegisterGlobalFunction("int Scoped(int value)",
        [](mini_as::GenericCall& call) { call.SetReturnInt(call.GetArgInt(0) + 2); }) == asSUCCESS);
    CHECK(engine->EndConfigGroup() == asSUCCESS);
    CHECK(engine->EndConfigGroup() == asERROR);
    CHECK(engine->SetDefaultAccessMask(0x2u) == 0x1u);
    CHECK(engine->RegisterGlobalFunction("int Hidden()",
        [](mini_as::GenericCall& call) { call.SetReturnInt(99); }) == asSUCCESS);
    CHECK(engine->SetDefaultNamespace("") == asSUCCESS);

    auto* visible = engine->GetModule("visible-controls", asGM_ALWAYS_CREATE);
    CHECK(visible->SetAccessMask(0x1u) == 0x2u);
    CHECK(visible->GetAccessMask() == 0x1u);
    CHECK(visible->AddScriptSection("visible.as",
        "namespace Scripts { int entry() { return HostTools::Scoped(40); } }") == asSUCCESS);
    CHECK(visible->Build() == asSUCCESS);
    CHECK(visible->SetDefaultNamespace("Scripts") == asSUCCESS);
    CHECK(std::string(visible->GetDefaultNamespace()) == "Scripts");
    CHECK(visible->GetFunctionByName("entry") != nullptr);
    CHECK(visible->GetFunctionByDecl("int entry()") != nullptr);
    const mini_as::BytecodeFunction* dynamic = nullptr;
    CHECK(visible->CompileFunction("dynamic-controls",
        "int dynamicEntry() { return entry(); }", 0, asCOMP_ADD_TO_MODULE,
        &dynamic) == asSUCCESS);
    CHECK(dynamic != nullptr);
    CHECK(dynamic->signature.name == "Scripts::dynamicEntry");
    CHECK(visible->GetFunctionByName("dynamicEntry") == dynamic);
    CHECK(engine->RemoveConfigGroup("runtime") == asCONFIG_GROUP_IS_IN_USE);

    auto context = engine->CreateContext();
    CHECK(context->Prepare(visible->GetFunctionByName("entry")) == asSUCCESS);
    CHECK(context->Execute() == asEXECUTION_FINISHED);
    CHECK(context->GetReturnDWord() == 42);
    CHECK(context->Prepare(dynamic) == asSUCCESS);
    CHECK(context->Execute() == asEXECUTION_FINISHED);
    CHECK(context->GetReturnDWord() == 42);

    auto* hidden = engine->GetModule("hidden-controls", asGM_ALWAYS_CREATE);
    hidden->SetAccessMask(0x1u);
    CHECK(hidden->AddScriptSection("hidden.as",
        "int main() { return HostTools::Hidden(); }") == asSUCCESS);
    CHECK(hidden->Build() == asERROR);
    CHECK(hidden->SetDefaultNamespace("bad::") == asINVALID_NAME);

    auto removable = CreateScriptEngine();
    CHECK(removable->BeginConfigGroup("temporary") == asSUCCESS);
    CHECK(removable->RegisterGlobalFunction("int Temporary()",
        [](mini_as::GenericCall& call) { call.SetReturnInt(1); }) == asSUCCESS);
    CHECK(removable->EndConfigGroup() == asSUCCESS);
    CHECK(removable->RemoveConfigGroup("temporary") == asSUCCESS);
    auto* removed = removable->GetModule("removed-controls", asGM_ALWAYS_CREATE);
    CHECK(removed->AddScriptSection("removed.as", "int main() { return Temporary(); }") == asSUCCESS);
    CHECK(removed->Build() == asERROR);
}

TEST_CASE(compat_facade_exposes_official_style_gc_controls_and_statistics) {
    auto engine = mini_as::compat::CreateScriptEngine();
    auto* module = engine->GetModule("gc", mini_as::compat::asGM_ALWAYS_CREATE);
    CHECK(module != nullptr);
    const char* source =
        "class Node { Node@ next; } "
        "Node@ makeCycle() { Node@ value = Node(); value.next = value; return value; }";
    CHECK(module->AddScriptSection("gc", source) == mini_as::compat::asSUCCESS);
    CHECK(module->Build() == mini_as::compat::asSUCCESS);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("Node@ makeCycle()")) ==
          mini_as::compat::asSUCCESS);
    CHECK(context->Execute() == mini_as::compat::asEXECUTION_FINISHED);
    mini_as::ObjectHandle root = context->GetReturnValue().As<mini_as::ObjectHandle>();
    context.reset();
    root = {};

    std::size_t detected = 0;
    engine->SetCircularRefDetectedCallback(
        [&](const mini_as::TypeInfo*, const mini_as::RefObject*) { ++detected; });
    CHECK(engine->GarbageCollect(0) == mini_as::compat::asINVALID_ARG);
    CHECK(engine->GarbageCollect(mini_as::compat::asGC_ONE_STEP, 1) ==
          mini_as::compat::asSUCCESS);
    CHECK(engine->GarbageCollect(mini_as::compat::asGC_FULL_CYCLE) ==
          mini_as::compat::asSUCCESS);
    std::uint32_t current = 99, destroyed = 0, cycles = 0, fresh = 99, freshDestroyed = 0;
    engine->GetGCStatistics(&current, &destroyed, &cycles, &fresh, &freshDestroyed);
    CHECK(current == 0);
    CHECK(destroyed == 1);
    CHECK(cycles == 1);
    CHECK(fresh == 0);
    CHECK(detected == 1);
}

TEST_CASE(official_style_facade_exposes_import_binding_controls) {
    using namespace mini_as::compat;
    auto engine = CreateScriptEngine();
    auto* source = engine->GetModule("facade-source", asGM_ALWAYS_CREATE);
    CHECK(source->AddScriptSection("source.as",
        "int Add(int value) { return value + 2; }") == asSUCCESS);
    CHECK(source->Build() == asSUCCESS);
    auto* consumer = engine->GetModule("facade-consumer", asGM_ALWAYS_CREATE);
    CHECK(consumer->AddScriptSection("consumer.as",
        "import int Add(int value) from \"facade-source\"; "
        "int main() { return Add(40); }") == asSUCCESS);
    CHECK(consumer->Build() == asSUCCESS);
    CHECK(consumer->GetImportedFunctionCount() == 1);
    CHECK(consumer->GetImportedFunctionIndexByDecl("int Add(int)") == 0);
    CHECK(std::string(consumer->GetImportedFunctionDeclaration(0)) == "int Add(int)");
    CHECK(std::string(consumer->GetImportedFunctionSourceModule(0)) == "facade-source");
    CHECK(consumer->BindImportedFunction(0, source->GetFunctionByName("Add")) == asSUCCESS);
    auto context = engine->CreateContext();
    CHECK(context->Prepare(consumer->GetFunctionByName("main")) == asSUCCESS);
    CHECK(context->Execute() == asEXECUTION_FINISHED);
    CHECK(context->GetReturnDWord() == 42);
    CHECK(consumer->UnbindAllImportedFunctions() == asSUCCESS);
    CHECK(consumer->BindAllImportedFunctions() == asSUCCESS);
}
