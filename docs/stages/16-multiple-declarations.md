# Stage 16: Multiple declarations

Local declaration statements now accept a comma-separated list, matching the
AngelScript form `int first = 1, second = first + 1, third;`.

The parser represents a multi-declaration statement as a `DeclList` whose
children are ordinary `VarDecl` nodes. This keeps initialization, type checking,
symbol declaration, and bytecode generation identical to a sequence of separate
declarations. Initializers are checked and executed from left to right, so a
later declarator can refer to an earlier one in the same statement.

Duplicate names remain illegal in one lexical scope, including duplicates
introduced inside the same comma-separated declaration.
