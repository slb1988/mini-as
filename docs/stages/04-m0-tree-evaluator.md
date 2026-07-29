# 04 - M0 tree evaluator

M0 proves the complete front-end pipe with `Print(42);`: tokenize, parse,
walk the expression tree, and cross into a host callback. A host function takes
a vector of `Value` objects and returns a `Value`; this is the smallest form of
AngelScript's portable generic calling convention.

Tree walking is intentionally temporary. It is easy to inspect because each
AST node directly triggers its semantic operation, but its C++ recursion makes
suspension and a script-visible call stack difficult. The bytecode stages will
replace it while retaining the same tokens, AST, values, and diagnostics.
