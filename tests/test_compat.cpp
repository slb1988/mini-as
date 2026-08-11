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
