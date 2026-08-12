# Stage 23: Break statements

`break` is valid inside `while`, `do`/`while`, `for`, and `switch`. The type
checker tracks breakable nesting and reports a compile error when no target is
available.

The bytecode compiler keeps a stack of control-flow contexts. Each `break` emits
an unresolved jump into the nearest context; that context patches its jumps to
the instruction immediately after the loop or switch. Nested switches and loops
therefore select the nearest target without special-case AST walks.
