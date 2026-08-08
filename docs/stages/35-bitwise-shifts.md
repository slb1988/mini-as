# Stage 35: Bitwise and shift operators

The expression grammar now includes `~`, `&`, `|`, `^`, `<<`, `>>`, and `>>>`
with separate precedence levels, plus all six compound-assignment forms. Only
integer operands are accepted in the current primitive model, and the result
keeps the left operand's declared type and width.

Dedicated bytecode instructions operate on the exact integer bits. `>>` follows
AngelScript's logical right-shift behavior, while `>>>` performs the arithmetic
right shift used by the official 2.38.0 compiler. Shift counts are normalized
to the operand width; results are truncated through `Value::Integer`, preserving
the existing narrow-integer wrap semantics.

The reusable constant evaluator implements the same operations, so bitwise
constant expressions can be used in switch cases without a runtime-only path.
Floating operands are diagnosed before bytecode generation.
