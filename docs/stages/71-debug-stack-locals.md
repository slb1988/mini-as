# Stage 71: stack locals and instruction locations

## Goal

This stage makes the VM's existing source locations useful to an embedded
debugger. A host can now inspect the current function, instruction location,
and named local variables for every script stack frame while a line callback is
running, while execution is suspended, or after an unhandled exception.

The semantic reference is AngelScript 2.38.0's `asIScriptContext` debugging
surface: `GetCallstackSize`, `GetFunction`, `GetLineNumber`, `GetVarCount`,
`GetVar`, `GetAddressOfVar`, and `IsVarInScope`. The native mini API remains
value-oriented; the later `mini_as::compat` facade can translate these queries
to official-style result codes and addresses.

## Public API

`ScriptContext` adds:

- `GetCallStackSize()` for the number of inspectable script frames;
- `GetFunction(stackLevel)` with level zero as the current function;
- `GetInstructionLocation(stackLevel)` for section, row, column, and offset;
- `GetLocals(stackLevel)` returning copied `LocalVariableInfo` records.

Each local record contains its source name, declared type, stable slot,
const/parameter flags, current value, and an `inScope` flag. Returning `Value`
copies avoids exposing VM stack addresses whose lifetime ends when execution
resumes. Out-of-range levels and non-inspectable states return empty results.

The existing exception-oriented `GetCallStack()` remains source compatible.
Its `StackFrameInfo` entries now also retain the function, instruction offset,
and local snapshot.

## Compiler and VM design

`BytecodeFunction::debugVariables` records named source variables only;
compiler-generated selector and receiver slots remain hidden. The compiler
opens and closes debug scopes together with lexical symbol scopes. Parameters
and implicit `this` cover the full function, while block, `for`, and `switch`
locals end at their lexical boundary.

The VM keeps live frames in its existing local-slot storage. Inspection unwraps
captured cells so a debugger sees the current captured value. On an unhandled
exception, `Fail` snapshots every frame before execution storage is released;
this preserves both the failing frame and its callers for later inspection.

Local debug tables are part of bytecode archives. Their addition advances the
mini bytecode format to version 2, with load-time validation of slots and scope
ranges.

## Verification

Tests cover:

- compiler-emitted names, types, parameter flags, and nested scope ranges;
- active line-callback and suspended inspection across current/caller frames;
- out-of-scope and invalid stack-level queries;
- exception-time values and exact failing/call instruction rows;
- bytecode save/load preservation of local debug metadata;
- differential output against the local AngelScript 2.38.0 tag for function,
  line, stack depth, and integer local values.

This stage deliberately exposes read-only snapshots. Mutating locals,
enumerating operand-stack temporaries, debugger breakpoints, and official-style
raw addresses are future compatibility/debugger work.
