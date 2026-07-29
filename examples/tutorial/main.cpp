#include "mini_as/engine.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "script.as";
    std::ifstream input(path, std::ios::binary);
    if (!input) { std::cerr << "Failed to open " << path << "\n"; return 1; }
    std::ostringstream source;
    source << input.rdbuf();

    auto engine = mini_as::CreateScriptEngine();
    engine->SetMessageCallback([](const mini_as::Diagnostic& message) {
        std::cerr << message.location.section << " (" << message.location.row << ", "
                  << message.location.column << "): " << message.message << "\n";
    });
    if (!engine->RegisterGlobalFunction("void Print(string &in)", [](mini_as::GenericCall& call) {
            std::cout << call.GetArgString(0);
        }) ||
        !engine->RegisterGlobalFunction("int GetSystemTime()", [](mini_as::GenericCall& call) {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            call.SetReturnInt(static_cast<std::int32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now).count() & 0x7fffffff));
        })) return 1;

    auto* module = engine->GetModule("tutorial", mini_as::ModulePolicy::AlwaysCreate);
    module->AddScriptSection("script.as", source.str());
    if (!module->Build()) return 1;
    const auto* function = module->GetFunctionByDecl("float calc(float, float)");
    if (!function) { std::cerr << "calc function not found\n"; return 1; }

    auto context = engine->CreateContext();
    context->Prepare(function);
    context->SetArgFloat(0, 3.14159265359f);
    context->SetArgFloat(1, 2.71828182846f);
    std::size_t lineBudget = 10000;
    context->SetLineCallback([&](mini_as::ScriptContext& current, const mini_as::SourceLocation&) {
        if (lineBudget-- == 0) current.Abort();
    });

    std::cout << "Executing the script.\n---\n";
    const auto state = context->Execute();
    std::cout << "---\n";
    if (state != mini_as::ExecutionState::Finished) {
        std::cerr << "Execution failed: " << context->GetExceptionString() << "\n";
        return 1;
    }
    std::cout << "The script function returned: " << context->GetReturnFloat() << "\n";
    return 0;
}

