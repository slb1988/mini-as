# 15 - Context controls and tutorial clone

Line cues now call the host before checking Abort or Suspend. A callback may
apply a time or instruction budget without instrumenting individual opcodes.
Suspension preserves all VM frames; abort exits at the next cue. Runtime traps
snapshot the current function and saved call frames with source locations.

The final tutorial follows the recognizable AngelScript flow: create engine,
install diagnostics, register `Print` and `GetSystemTime`, add a script section,
build a module, find `float calc(float, float)`, prepare a context, set arguments,
execute, and read the float return value.

The end-to-end test injects a deterministic clock and captures Print output.
Additional tests cover callback-driven suspend/resume, callback abort, and a
three-level exception stack trace.

