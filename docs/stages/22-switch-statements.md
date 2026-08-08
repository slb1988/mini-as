# Stage 22: Switch statements

`switch` accepts an integer selector, integer constant-expression `case` labels,
and at most one `default` clause. The type checker evaluates labels with the
shared constant-expression evaluator and rejects duplicate values before
bytecode generation.

Bytecode stores the selector once, emits a comparison dispatch table, and then
lays clause bodies out in source order. No implicit jump is inserted between
clause bodies, preserving AngelScript's fall-through behavior. If no case
matches, execution jumps to `default` or past the switch when no default exists.
