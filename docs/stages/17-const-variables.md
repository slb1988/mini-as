# Stage 17: Const variables

Local variables can now be qualified with `const`. Their initializer is checked
and emitted like an ordinary declaration, but the type checker marks the symbol
as read-only for the rest of its lexical lifetime.

Assignments to a const local are rejected before bytecode is generated. The
same protection follows a const local through field access, so an object reached
through a const variable cannot be mutated through that expression.

Const declarations may use the comma-separated declaration form introduced in
stage 16. As in AngelScript, const values remain readable in ordinary expressions
and can be used by later declarations.
