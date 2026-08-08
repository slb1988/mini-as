# Stage 36: Exponent operators

`**` has its own precedence above multiplication and follows AngelScript
2.38.0's left associativity. `**=` uses the shared lvalue pipeline, so locals,
globals, and fields are evaluated once and narrowed back to their declared type
after the operation.

Typed `POW_I`, `POW_F`, and `POW_D` instructions preserve the normal numeric
promotion result. Integer powers use exponentiation by squaring, return zero for
a negative exponent with a non-zero base, and raise a located `exponent
overflow` exception for overflow and the `0 ** 0` domain error. Floating powers
use the corresponding standard-library `pow` operation.

The constant evaluator folds the same operator for switch cases and other
compile-time consumers. Non-numeric operands remain compile diagnostics.
