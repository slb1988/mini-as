# 08 - Control flow, backpatching, and suspension

Structured control flow lowers to unstructured jumps. The compiler emits a
placeholder target for `JZ`, compiles the branch body, then patches the target
with the first instruction after that body. A while loop records its condition
index and emits a backward `JMP` after the body.

`&&` and `||` use the same mechanism rather than eager boolean opcodes. Their
right operand is skipped when the left side decides the result, which is
verified with a deliberately unreachable division by zero.

Every statement starts with `SUSPEND`. The normal fast path is a flag check;
when requested, the VM returns with its heap stack, locals, and `pc` intact.
Calling `Continue` resumes at the next instruction without reconstructing any
C++ call stack.
