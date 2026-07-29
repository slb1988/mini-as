# 01 - Source, diagnostics, types, and values

`DataType` is compiler knowledge: it describes what an expression is allowed
to do before execution. `Value` is runtime storage: it contains the actual bits
used by the evaluator or VM. Keeping them separate is the foundation for a
statically typed engine.

AngelScript stores primitive VM values in DWORD-sized slots. This teaching
engine starts with a tagged `std::variant`, trading compactness for transparent
and safe code. Later bytecode is still typed, so the same compiler invariants
are exercised.

Every token, AST node, instruction line cue, and runtime exception carries a
`SourceLocation`. Diagnostics are collected as data and may also be streamed to
a host callback; compiler code never writes directly to the console.
