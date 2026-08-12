#pragma once

#include <cstdint>
#include <limits>

namespace mini_as {

template <typename Tag>
struct SymbolId {
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

    bool IsValid() const { return value != std::numeric_limits<std::uint32_t>::max(); }
    friend bool operator==(SymbolId left, SymbolId right) { return left.value == right.value; }
    friend bool operator!=(SymbolId left, SymbolId right) { return !(left == right); }
};

struct TypeIdTag;
struct FunctionIdTag;
struct VariableIdTag;
struct GlobalIdTag;

using TypeId = SymbolId<TypeIdTag>;
using FunctionId = SymbolId<FunctionIdTag>;
using VariableId = SymbolId<VariableIdTag>;
using GlobalId = SymbolId<GlobalIdTag>;

} // namespace mini_as
