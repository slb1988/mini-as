# Stage 26: Prefix and postfix increment

Numeric local, global, and field lvalues support prefix and postfix `++` and
`--`. Const and non-lvalue operands are rejected during type checking.

Prefix form stores and returns the new value. Postfix form preserves the old
value below the update sequence and discards the stored value afterward. Field
updates evaluate their receiver once; a small `SWAP` stack opcode arranges the
receiver and preserved value without introducing temporary user-visible locals.
