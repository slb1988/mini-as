#pragma once

#include "mini_as/engine.hpp"

#include <iosfwd>

namespace mini_as::detail {

struct BytecodeArchive {
    BytecodeModule bytecode;
    ModuleCompilationEnvironment environment;
    std::vector<FunctionId> removedFunctions;
    std::vector<std::shared_ptr<const SyntaxTree>> definitionTrees;
};

bool WriteBytecodeArchive(std::ostream& output, const BytecodeArchive& archive,
                          std::string& error);
bool ReadBytecodeArchive(std::istream& input, BytecodeArchive& archive,
                         std::string& error);

} // namespace mini_as::detail
