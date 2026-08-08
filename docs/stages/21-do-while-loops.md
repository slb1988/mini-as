# Stage 21: Do-while loops

`do`/`while` loops are represented with the body first and condition second,
mirroring their execution order. The trailing semicolon is part of the syntax
and produces a targeted parser diagnostic when omitted.

Type checking requires a boolean condition. Bytecode enters the body directly,
evaluates the condition after each iteration, exits on false, and otherwise
jumps back to the body. This guarantees one execution even when the initial
condition is false.
