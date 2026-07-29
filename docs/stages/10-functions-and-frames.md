# 10 - Two-pass functions and explicit frames

The builder first allocates every script function and records its signature,
then compiles bodies. A call may therefore target a function declared later in
the file, and recursion is no different from any other call.

`CALL` pops arguments into fresh local slots and saves the caller's function,
program counter, and local array in a heap-owned frame. `RET` restores that
frame and pushes the returned value for the caller expression. The operand
stack remains separate from control frames, simplifying cleanup and inspection.

The call depth limit is explicit and produces a script exception. Tests cover
forward recursion and verify that caller locals survive a nested call.
