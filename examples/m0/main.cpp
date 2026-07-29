#include "mini_as/interpreter.hpp"

#include <iostream>

int main() {
    mini_as::DiagnosticSink diagnostics([](const mini_as::Diagnostic& diagnostic) {
        std::cerr << diagnostic.location.row << ":" << diagnostic.location.column
                  << ": " << diagnostic.message << "\n";
    });
    mini_as::TreeInterpreter interpreter(diagnostics);
    interpreter.RegisterFunction("Print", [](const std::vector<mini_as::Value>& arguments) {
        if (arguments.size() != 1) throw std::runtime_error("Print expects one argument");
        std::cout << arguments[0].ToString() << "\n";
        return mini_as::Value{};
    });
    return mini_as::RunTreeScript("Print(40 + 2);", interpreter, diagnostics) ? 0 : 1;
}

