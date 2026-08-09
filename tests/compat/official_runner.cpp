#include <angelscript.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

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
    engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, true);
    engine->SetMessageCallback(asFUNCTION(MessageCallback), nullptr, asCALL_CDECL);
    asIScriptModule* module = engine->GetModule("compat", asGM_ALWAYS_CREATE);
    const std::string source = ReadFile(argv[1]);
    module->AddScriptSection("compat.as", source.c_str(), source.size());
    if (module->Build() < 0) { engine->ShutDownAndRelease(); return 4; }
    asIScriptFunction* function = module->GetFunctionByDecl("int main()");
    if (!function) { engine->ShutDownAndRelease(); return 5; }
    asIScriptContext* context = engine->CreateContext();
    context->Prepare(function);
    const int state = context->Execute();
    if (state != asEXECUTION_FINISHED) {
        context->Release();
        engine->ShutDownAndRelease();
        return 6;
    }
    std::cout << "state=finished\nreturn=int:" << context->GetReturnDWord() << '\n';
    context->Release();
    engine->ShutDownAndRelease();
    return 0;
}
