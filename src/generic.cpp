#include "mini_as/generic.hpp"

#include "mini_as/tokenizer.hpp"

#include <algorithm>
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

GenericCall::GenericCall(std::vector<Value>& arguments, Value object)
    : arguments_(arguments), object_(std::move(object)) {}
std::size_t GenericCall::GetArgCount() const { return arguments_.size(); }
const ObjectHandle& GenericCall::GetObject() const { return object_.As<ObjectHandle>(); }
const Value& GenericCall::GetObjectValue() const { return object_; }
const Value& GenericCall::GetArg(std::size_t index) const { return arguments_.at(index); }
std::int32_t GenericCall::GetArgInt(std::size_t index) const { return GetArg(index).As<std::int32_t>(); }
float GenericCall::GetArgFloat(std::size_t index) const { return GetArg(index).As<float>(); }
double GenericCall::GetArgDouble(std::size_t index) const { return GetArg(index).As<double>(); }
bool GenericCall::GetArgBool(std::size_t index) const { return GetArg(index).As<bool>(); }
const std::string& GenericCall::GetArgString(std::size_t index) const { return GetArg(index).As<std::string>(); }
const ObjectHandle& GenericCall::GetArgObject(std::size_t index) const { return GetArg(index).As<ObjectHandle>(); }
void GenericCall::SetArg(std::size_t index, Value value) { arguments_.at(index) = std::move(value); }
void GenericCall::SetArgInt(std::size_t index, std::int32_t value) { SetArg(index, Value(value)); }
void GenericCall::SetArgFloat(std::size_t index, float value) { SetArg(index, Value(value)); }
void GenericCall::SetArgDouble(std::size_t index, double value) { SetArg(index, Value(value)); }
void GenericCall::SetArgBool(std::size_t index, bool value) { SetArg(index, Value(value)); }
void GenericCall::SetArgString(std::size_t index, std::string value) {
    SetArg(index, Value(std::move(value)));
}
void GenericCall::SetArgObject(std::size_t index, ObjectHandle value) {
    SetArg(index, Value(std::move(value)));
}
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
    Tokenizer tokenizer("registration", declaration, diagnostics);
    const auto tokens = tokenizer.ScanAll();
    if (diagnostics.HasErrors()) return std::nullopt;
    std::size_t index = 0;
    FunctionSignature signature;
    if (index < tokens.size() && tokens[index].kind == TokenKind::KwConst) {
        signature.returnReferenceConst = true;
        ++index;
    }
    signature.returnType = ReadType(tokens, index);
    if (index < tokens.size() && tokens[index].kind == TokenKind::Amp) {
        signature.returnsReference = true;
        ++index;
    }
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
        ParameterMode mode = ParameterMode::Value;
        if (index < tokens.size() && tokens[index].kind == TokenKind::Amp) {
            ++index;
            if (index < tokens.size() && tokens[index].kind == TokenKind::KwIn) {
                mode = ParameterMode::In;
                ++index;
            } else if (index < tokens.size() && tokens[index].kind == TokenKind::KwOut) {
                mode = ParameterMode::Out;
                ++index;
            } else if (index < tokens.size() && tokens[index].kind == TokenKind::KwInOut) {
                mode = ParameterMode::InOut;
                ++index;
            } else mode = ParameterMode::InOut;
        }
        signature.parameterModes.push_back(mode);
        if (index < tokens.size() && tokens[index].kind == TokenKind::Identifier)
            signature.parameterNames.push_back(tokens[index].lexeme);
        else signature.parameterNames.emplace_back();
        while (index < tokens.size() && tokens[index].kind != TokenKind::Comma &&
               tokens[index].kind != TokenKind::RightParen) ++index;
        if (index < tokens.size() && tokens[index].kind == TokenKind::Comma) ++index;
    }
    if (index >= tokens.size() || tokens[index].kind != TokenKind::RightParen) {
        diagnostics.Report(tokens.back().location, Severity::Error, "expected ')' after parameters");
        return std::nullopt;
    }
    ++index;
    if (index < tokens.size() && tokens[index].kind == TokenKind::KwConst) {
        signature.readOnlyMethod = true;
        ++index;
    }
    if (index < tokens.size() && tokens[index].kind != TokenKind::End) {
        diagnostics.Report(tokens[index].location, Severity::Error, "unexpected text after declaration");
        return std::nullopt;
    }
    return signature;
}

std::optional<GlobalSignature> ParseGlobalPropertyDeclaration(
    std::string_view declaration, DiagnosticSink& diagnostics) {
    Tokenizer tokenizer("registration", declaration, diagnostics);
    const auto tokens = tokenizer.ScanAll();
    if (diagnostics.HasErrors()) return std::nullopt;
    std::size_t index = 0;
    GlobalSignature signature;
    signature.host = true;
    if (index < tokens.size() && tokens[index].kind == TokenKind::KwConst) {
        signature.isConst = true;
        ++index;
    }
    signature.type = ReadType(tokens, index);
    if (!signature.type.IsValid() || signature.type == DataType::Void() ||
        index >= tokens.size() || tokens[index].kind != TokenKind::Identifier) {
        diagnostics.Report(tokens[std::min(index, tokens.size() - 1)].location, Severity::Error,
                           "invalid global property declaration");
        return std::nullopt;
    }
    signature.name = tokens[index++].lexeme;
    if (index >= tokens.size() || tokens[index].kind != TokenKind::End) {
        diagnostics.Report(tokens[std::min(index, tokens.size() - 1)].location, Severity::Error,
                           "unexpected text after global property declaration");
        return std::nullopt;
    }
    return signature;
}

} // namespace mini_as
