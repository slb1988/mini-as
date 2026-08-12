# Stage 27: Conditional expressions

The right-associative `condition ? whenTrue : whenFalse` expression requires a
boolean condition. Branch types must match or have one unambiguous implicit
conversion; the current numeric subset promotes `int` to `float` when needed.

Bytecode evaluates the condition and jumps around the unselected branch, so
side effects and runtime errors occur only in the chosen expression. Both paths
leave one value of the inferred result type on the operand stack. Constant
evaluation follows the same short-circuit rule.
