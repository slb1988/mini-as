#pragma once

#include "mini_as/type_checker.hpp"

#include <functional>
#include <optional>

namespace mini_as {

class GenericCall {
public:
    explicit GenericCall(const std::vector<Value>& arguments);
    std::size_t GetArgCount() const;
    const Value& GetArg(std::size_t index) const;
    std::int32_t GetArgInt(std::size_t index) const;
    float GetArgFloat(std::size_t index) const;
    bool GetArgBool(std::size_t index) const;
    const std::string& GetArgString(std::size_t index) const;
    void SetReturn(Value value);
    void SetReturnInt(std::int32_t value);
    void SetReturnFloat(float value);
    void SetReturnBool(bool value);
    void SetReturnString(std::string value);
    void SetException(std::string message);
    const Value& ReturnValue() const;
    const std::string& Exception() const;

private:
    const std::vector<Value>& arguments_;
    Value returnValue_;
    std::string exception_;
};

using GenericFunction = std::function<void(GenericCall&)>;

struct RegisteredHostFunction {
    FunctionSignature signature;
    GenericFunction callback;
};

std::optional<FunctionSignature> ParseFunctionDeclaration(
    std::string_view declaration, DiagnosticSink& diagnostics);

} // namespace mini_as

