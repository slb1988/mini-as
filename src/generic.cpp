#include "mini_as/generic.hpp"

#include "mini_as/tokenizer.hpp"

#include <utility>

namespace mini_as {
namespace {

DataType ReadType(const std::vector<Token>& tokens, std::size_t& index) {
    if (index >= tokens.size()) return DataType::Invalid();
    DataType type;
    switch (tokens[index].kind) {
    case TokenKind::KwVoid: type = DataType::Void(); break;
    case TokenKind::KwBool: type = DataType::Bool(); break;
    case TokenKind::KwInt8: type = DataType::Int8(); break;
    case TokenKind::KwInt16: type = DataType::Int16(); break;
    case TokenKind::KwInt: type = DataType::Int(); break;
    case TokenKind::KwInt64: type = DataType::Int64(); break;
    case TokenKind::KwUInt8: type = DataType::UInt8(); break;
    case TokenKind::KwUInt16: type = DataType::UInt16(); break;
    case TokenKind::KwUInt: type = DataType::UInt(); break;
    case TokenKind::KwUInt64: type = DataType::UInt64(); break;
    case TokenKind::KwFloat: type = DataType::Float(); break;
    case TokenKind::KwDouble: type = DataType::Double(); break;
    case TokenKind::KwString: type = DataType::String(); break;
    case TokenKind::Identifier: type = DataType::Object(tokens[index].lexeme); break;
    default: return DataType::Invalid();
    }
    ++index;
    if (index < tokens.size() && tokens[index].kind == TokenKind::At) { type.isHandle = true; ++index; }
    return type;
}

} // namespace

GenericCall::GenericCall(const std::vector<Value>& arguments) : arguments_(arguments) {}
std::size_t GenericCall::GetArgCount() const { return arguments_.size(); }
const Value& GenericCall::GetArg(std::size_t index) const { return arguments_.at(index); }
std::int32_t GenericCall::GetArgInt(std::size_t index) const { return GetArg(index).As<std::int32_t>(); }
float GenericCall::GetArgFloat(std::size_t index) const { return GetArg(index).As<float>(); }
double GenericCall::GetArgDouble(std::size_t index) const { return GetArg(index).As<double>(); }
bool GenericCall::GetArgBool(std::size_t index) const { return GetArg(index).As<bool>(); }
const std::string& GenericCall::GetArgString(std::size_t index) const { return GetArg(index).As<std::string>(); }
const ObjectHandle& GenericCall::GetArgObject(std::size_t index) const { return GetArg(index).As<ObjectHandle>(); }
void GenericCall::SetReturn(Value value) { returnValue_ = std::move(value); }
void GenericCall::SetReturnInt(std::int32_t value) { SetReturn(Value(value)); }
void GenericCall::SetReturnFloat(float value) { SetReturn(Value(value)); }
void GenericCall::SetReturnDouble(double value) { SetReturn(Value(value)); }
void GenericCall::SetReturnBool(bool value) { SetReturn(Value(value)); }
void GenericCall::SetReturnString(std::string value) { SetReturn(Value(std::move(value))); }
void GenericCall::SetReturnObject(ObjectHandle value) { SetReturn(Value(std::move(value))); }
void GenericCall::SetException(std::string message) { exception_ = std::move(message); }
const Value& GenericCall::ReturnValue() const { return returnValue_; }
const std::string& GenericCall::Exception() const { return exception_; }

std::optional<FunctionSignature> ParseFunctionDeclaration(
    std::string_view declaration, DiagnosticSink& diagnostics) {
    std::string normalized(declaration);
    for (char& ch : normalized) if (ch == '&') ch = ' ';
    Tokenizer tokenizer("registration", normalized, diagnostics);
    const auto tokens = tokenizer.ScanAll();
    if (diagnostics.HasErrors()) return std::nullopt;
    std::size_t index = 0;
    FunctionSignature signature;
    signature.returnType = ReadType(tokens, index);
    if (!signature.returnType.IsValid() || index >= tokens.size() || tokens[index].kind != TokenKind::Identifier) {
        diagnostics.Report(tokens[std::min(index, tokens.size() - 1)].location, Severity::Error,
                           "invalid function declaration");
        return std::nullopt;
    }
    signature.name = tokens[index++].lexeme;
    signature.host = true;
    if (index >= tokens.size() || tokens[index++].kind != TokenKind::LeftParen) {
        diagnostics.Report(tokens[std::min(index, tokens.size() - 1)].location, Severity::Error, "expected '('");
        return std::nullopt;
    }
    while (index < tokens.size() && tokens[index].kind != TokenKind::RightParen) {
        DataType parameter = ReadType(tokens, index);
        if (!parameter.IsValid() || parameter == DataType::Void()) {
            diagnostics.Report(tokens[std::min(index, tokens.size() - 1)].location, Severity::Error,
                               "invalid parameter type");
            return std::nullopt;
        }
        signature.parameters.push_back(std::move(parameter));
        while (index < tokens.size() && tokens[index].kind != TokenKind::Comma &&
               tokens[index].kind != TokenKind::RightParen) ++index;
        if (index < tokens.size() && tokens[index].kind == TokenKind::Comma) ++index;
    }
    if (index >= tokens.size() || tokens[index].kind != TokenKind::RightParen) {
        diagnostics.Report(tokens.back().location, Severity::Error, "expected ')' after parameters");
        return std::nullopt;
    }
    ++index;
    if (index < tokens.size() && tokens[index].kind != TokenKind::End) {
        diagnostics.Report(tokens[index].location, Severity::Error, "unexpected text after declaration");
        return std::nullopt;
    }
    return signature;
}

} // namespace mini_as
