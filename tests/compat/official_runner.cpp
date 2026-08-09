#include <angelscript.h>
#include <weakref.h>

#include <fstream>
#include <iostream>
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
    if (engine->RegisterObjectType("HostRef", 0, asOBJ_REF) < 0 ||
        engine->RegisterObjectBehaviour("HostRef", asBEHAVE_FACTORY,
            "HostRef@ f(int value)", asFUNCTION(CompatReferenceFactory), asCALL_GENERIC) < 0 ||
        engine->RegisterObjectBehaviour("HostRef", asBEHAVE_ADDREF,
            "void f()", asMETHOD(CompatReference, AddRef), asCALL_THISCALL) < 0 ||
        engine->RegisterObjectBehaviour("HostRef", asBEHAVE_RELEASE,
            "void f()", asMETHOD(CompatReference, Release), asCALL_THISCALL) < 0 ||
        engine->RegisterObjectMethod("HostRef", "int get() const",
            asMETHOD(CompatReference, Get), asCALL_THISCALL) < 0) {
        engine->ShutDownAndRelease();
        return 5;
    }
    asIScriptModule* module = engine->GetModule("compat", asGM_ALWAYS_CREATE);
    const std::string source = ReadFile(argv[1]);
    module->AddScriptSection("compat.as", source.c_str(), source.size());
    if (module->Build() < 0) { engine->ShutDownAndRelease(); return 6; }
    asIScriptFunction* function = module->GetFunctionByDecl("int main()");
    if (!function) { engine->ShutDownAndRelease(); return 7; }
    asIScriptContext* context = engine->CreateContext();
    context->Prepare(function);
    const int state = context->Execute();
    if (state != asEXECUTION_FINISHED) {
        context->Release();
        engine->ShutDownAndRelease();
        return 8;
    }
    std::cout << "state=finished\nreturn=int:" << context->GetReturnDWord() << '\n';
    context->Release();
    engine->ShutDownAndRelease();
    return 0;
}
