# Stage 50: try-catch blocks

Scripts can now wrap a statement block in `try { ... } catch { ... }`. The
catch block has its own lexical scope and runs only when execution in the try
block raises a runtime exception. Normal completion jumps over the catch.
Malformed statements without a following catch block are rejected by the
parser.

Each bytecode function records immutable exception-table entries containing a
half-open try instruction range and its catch target. No raw AST or instruction
pointer is retained. Nested ranges choose the smallest enclosing handler, so
the nearest catch wins. The table representation also keeps normal execution
free of enter/leave-handler opcodes.

The VM tracks an operand-stack baseline for every call frame. When an exception
is raised it first searches the current function, then walks callers using the
saved call-site program counters. Frames above the selected handler are
released, the operand stack is restored to that frame's baseline, and execution
continues at the catch target. This catches arithmetic, null receiver, explicit
host-call, and other runtime exceptions without catching cooperative suspend
or abort states.

If no handler exists, the prior behavior is preserved: execution ends in the
exception state with the original failing instruction location and call stack.
A caught exception is cleared from the public context result. Copy-out code
after a failed `out` or `inout` call is skipped during unwinding, so partial
updates are not published.

Tests cover nested handlers, exceptions raised in called script functions,
normal fall-through, malformed syntax, exception-table targets, and preservation
of existing uncaught-exception location behavior. The differential script
produces the same result under mini_as and AngelScript 2.38.0.
