# 07 - Heap-backed stack VM

The VM owns a value stack, local slots, and a program counter. Its dispatch loop
is `while (active) switch(opcode)` and never recurses through the C++ call stack.
That separation is the prerequisite for script call frames, suspension, stack
inspection, and deterministic limits.

Typed opcodes make the hot path direct: `ADD_I` can read two integers without
runtime overload resolution. Potentially failing instructions first identify
their exact instruction location and then transition the VM to `Exception`, so
the host receives a stable machine state rather than a C++ exception.

The tests execute compiler output rather than handcrafted instructions and
cover both a mixed numeric calculation and the division-by-zero failure path.
