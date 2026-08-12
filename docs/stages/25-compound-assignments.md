# Stage 25: Compound assignments

Numeric lvalues support `+=`, `-=`, `*=`, `/=`, and `%=`. String lvalues also
support `+=` through the existing concatenation conversion rules. The type
checker validates the operator result before assigning it back and preserves
const protection.

Compilation uses the unified `LValueRef`. Locals and globals load, operate, and
store through stable IDs. Field assignment evaluates the receiver once, keeps a
copy on the operand stack, loads the old field value, performs the operation,
and stores the result. The expression still evaluates to the assigned value.

Arithmetic runtime failures retain the compound-assignment source location.
