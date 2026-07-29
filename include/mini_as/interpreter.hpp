#pragma once

#include "mini_as/parser.hpp"

#include <functional>
#include <string>
#include <unordered_map>

namespace mini_as {

using TreeHostFunction = std::function<Value(const std::vector<Value>&)>;

class TreeInterpreter {
public:
    explicit TreeInterpreter(DiagnosticSink& diagnostics);
    void RegisterFunction(std::string name, TreeHostFunction function);
    Value Execute(AstNode* root);

private:
    Value Evaluate(AstNode* node);
    Value EvaluateBinary(AstNode* node);
    Value EvaluateUnary(AstNode* node);
    Value EvaluateCall(AstNode* node);
    Value DecodeLiteral(const Token& token);
    void RuntimeError(const AstNode* node, std::string message);

    DiagnosticSink& diagnostics_;
    std::unordered_map<std::string, TreeHostFunction> functions_;
};

bool RunTreeScript(std::string_view source, TreeInterpreter& interpreter,
                   DiagnosticSink& diagnostics, std::string section = "script");

} // namespace mini_as

