#include <angelscript.h>
#include <weakref.h>

#include <fstream>
#include <iostream>
#include <new>
#include <sstream>
#include <string>

namespace {

class CompatReference {
public:
    explicit CompatReference(int value) : value(value) {}
    void AddRef() { ++references_; }
    void Release() { if (--references_ == 0) delete this; }
    int Get() const { return value; }
    int value;

private:
    ~CompatReference() = default;
    int references_ = 1;
};

struct CompatValue {
    int value = 42;
};

void CompatValueConstruct(asIScriptGeneric* call) {
    new(call->GetObject()) CompatValue();
}

void CompatValueDestruct(asIScriptGeneric* call) {
    static_cast<CompatValue*>(call->GetObject())->~CompatValue();
}

void CompatValueCopyConstruct(asIScriptGeneric* call) {
    const auto* source = static_cast<const CompatValue*>(call->GetArgObject(0));
    new(call->GetObject()) CompatValue(*source);
}

void CompatValueAssign(asIScriptGeneric* call) {
    auto* destination = static_cast<CompatValue*>(call->GetObject());
    const auto* source = static_cast<const CompatValue*>(call->GetArgObject(0));
    *destination = *source;
    call->SetReturnAddress(destination);
}

void ReadHostValue(asIScriptGeneric* call) {
    const auto* value = static_cast<const CompatValue*>(call->GetArgObject(0));
    call->SetReturnDWord(static_cast<asDWORD>(value ? value->value : 0));
}

void GetHostValue(asIScriptGeneric* call) {
    const auto* value = static_cast<const CompatValue*>(call->GetObject());
    call->SetReturnDWord(static_cast<asDWORD>(value ? value->value : 0));
}

void Lift(asIScriptGeneric* call) {
    call->SetReturnDWord(call->GetArgDWord(0) + 2);
}

void CompatReferenceFactory(asIScriptGeneric* call) {
    call->SetReturnAddress(new CompatReference(static_cast<int>(call->GetArgDWord(0))));
}

std::string ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream source;
    source << input.rdbuf();
    return source.str();
}

void MessageCallback(const asSMessageInfo* message, void*) {
    std::cerr << message->row << ':' << message->col << ':' << message->message << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    asIScriptEngine* engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    if (!engine) return 3;
    int hostCounter = 40;
    engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, true);
    engine->SetMessageCallback(asFUNCTION(MessageCallback), nullptr, asCALL_CDECL);
    RegisterScriptWeakRef(engine);
    if (engine->RegisterGlobalProperty("int hostCounter", &hostCounter) < 0) {
        engine->ShutDownAndRelease();
        return 4;
    }
    if (engine->RegisterEnum("HostColor") < 0 ||
        engine->RegisterEnumValue("HostColor", "HostRed", 40) < 0 ||
        engine->RegisterEnumValue("HostColor", "HostBlue", 42) < 0 ||
        engine->RegisterTypedef("HostScore", "int") < 0 ||
        engine->RegisterFuncdef("HostScore HostTransform(HostScore value)") < 0 ||
        engine->RegisterGlobalFunction("HostScore Lift(HostScore value)",
            asFUNCTION(Lift), asCALL_GENERIC) < 0) {
        engine->ShutDownAndRelease();
        return 5;
    }
    asITypeInfo* reflectedEnum = engine->GetTypeInfoByName("HostColor");
    asITypeInfo* reflectedAlias = engine->GetTypeInfoByName("HostScore");
    asITypeInfo* reflectedFuncdef = engine->GetFuncdefCount() == 1
        ? engine->GetFuncdefByIndex(0) : nullptr;
    asIScriptFunction* reflectedLift =
        engine->GetGlobalFunctionByDecl("HostScore Lift(HostScore)");
    if (!reflectedEnum || reflectedEnum->GetEnumValueCount() != 2 ||
        !reflectedAlias || reflectedAlias->GetTypedefTypeId() < 0 ||
        !reflectedFuncdef || !reflectedFuncdef->GetFuncdefSignature() || !reflectedLift) {
        engine->ShutDownAndRelease();
        return 5;
    }
    if (engine->RegisterObjectType("HostValue", sizeof(CompatValue),
            asOBJ_VALUE | asGetTypeTraits<CompatValue>()) < 0 ||
        engine->RegisterObjectBehaviour("HostValue", asBEHAVE_CONSTRUCT, "void f()",
            asFUNCTION(CompatValueConstruct), asCALL_GENERIC) < 0 ||
        engine->RegisterObjectBehaviour("HostValue", asBEHAVE_DESTRUCT, "void f()",
            asFUNCTION(CompatValueDestruct), asCALL_GENERIC) < 0 ||
        engine->RegisterObjectBehaviour("HostValue", asBEHAVE_CONSTRUCT,
            "void f(const HostValue &in)", asFUNCTION(CompatValueCopyConstruct),
            asCALL_GENERIC) < 0 ||
        engine->RegisterObjectMethod("HostValue",
            "HostValue &opAssign(const HostValue &in)",
            asFUNCTION(CompatValueAssign), asCALL_GENERIC) < 0 ||
        engine->RegisterObjectMethod("HostValue", "int get() const",
            asFUNCTION(GetHostValue), asCALL_GENERIC) < 0 ||
        engine->RegisterGlobalFunction("int ReadHostValue(HostValue value)",
            asFUNCTION(ReadHostValue), asCALL_GENERIC) < 0) {
        engine->ShutDownAndRelease();
        return 6;
    }
    if (engine->RegisterObjectType("HostRef", 0, asOBJ_REF) < 0 ||
        engine->RegisterObjectProperty("HostRef", "int value",
            asOFFSET(CompatReference, value)) < 0 ||
        engine->RegisterObjectBehaviour("HostRef", asBEHAVE_FACTORY,
            "HostRef@ f(int value)", asFUNCTION(CompatReferenceFactory), asCALL_GENERIC) < 0 ||
        engine->RegisterObjectBehaviour("HostRef", asBEHAVE_ADDREF,
            "void f()", asMETHOD(CompatReference, AddRef), asCALL_THISCALL) < 0 ||
        engine->RegisterObjectBehaviour("HostRef", asBEHAVE_RELEASE,
            "void f()", asMETHOD(CompatReference, Release), asCALL_THISCALL) < 0 ||
        engine->RegisterObjectMethod("HostRef", "int get() const",
            asMETHOD(CompatReference, Get), asCALL_THISCALL) < 0) {
        engine->ShutDownAndRelease();
        return 7;
    }
    asIScriptModule* module = engine->GetModule("compat", asGM_ALWAYS_CREATE);
    const std::string source = ReadFile(argv[1]);
    module->AddScriptSection("compat.as", source.c_str(), source.size());
    if (module->Build() < 0) { engine->ShutDownAndRelease(); return 8; }
    if (std::string(argv[1]).find("registered_named_types") != std::string::npos &&
        (module->GetGlobalVarCount() != 2 ||
         module->GetGlobalVarIndexByDecl("HostColor selected") < 0 ||
         module->GetGlobalVarIndexByDecl("HostTransform@ transform") < 0)) {
        engine->ShutDownAndRelease();
        return 8;
    }
    if (std::string(argv[1]).find("registered_named_types") != std::string::npos) {
        asIScriptFunction* detached = nullptr;
        if (module->CompileFunction("dynamic-detached",
                "int detached_probe() { return main(); }", 0, 0, &detached) < 0 ||
            !detached || module->GetFunctionByDecl("int detached_probe()") ||
            module->CompileFunction("dynamic-added",
                "int dynamic_probe() { return main(); }", 0,
                asCOMP_ADD_TO_MODULE, nullptr) < 0 ||
            !module->GetFunctionByDecl("int dynamic_probe()")) {
            if (detached) detached->Release();
            engine->ShutDownAndRelease();
            return 8;
        }
        detached->Release();
        asIScriptFunction* added = module->GetFunctionByDecl("int dynamic_probe()");
        if (!added || module->CompileFunction("dynamic-caller",
                "int dynamic_caller() { return dynamic_probe(); }", 0,
                asCOMP_ADD_TO_MODULE, nullptr) < 0 ||
            module->RemoveFunction(added) < 0 ||
            module->GetFunctionByDecl("int dynamic_probe()") ||
            !module->GetFunctionByDecl("int dynamic_caller()")) {
            engine->ShutDownAndRelease();
            return 8;
        }
    }
    asIScriptFunction* function = module->GetFunctionByDecl("int main()");
    if (!function) { engine->ShutDownAndRelease(); return 9; }
    asIScriptContext* context = engine->CreateContext();
    context->Prepare(function);
    const int state = context->Execute();
    if (state != asEXECUTION_FINISHED) {
        context->Release();
        engine->ShutDownAndRelease();
        return 9;
    }
    std::cout << "state=finished\nreturn=int:" << context->GetReturnDWord() << '\n';
    context->Release();
    engine->ShutDownAndRelease();
    return 0;
}
