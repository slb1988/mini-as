# 05 - Static types and lexical scopes

Type checking annotates every expression node before bytecode exists. A scope
stack maps names to `DataType`; lookup walks from the innermost block outward,
while declaration checks only the current block. Shadowing is therefore legal
and duplicate declarations are not.

Functions are predeclared before bodies are checked. Each expression returns a
type to its parent, the small equivalent of AngelScript's expression context.
The only general implicit conversion is `int` to `float`; string concatenation
has an explicit formatting rule for tutorial compatibility.

Tests are now registered by feature. Every later behavior is delivered with a
positive test and relevant compiler/runtime failure tests in the same commit.
