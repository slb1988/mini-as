# Stage 90: suspended-context serialization

## Goal

Persist a cooperatively suspended `ScriptContext` and resume it in a fresh
engine after the corresponding bytecode modules have been loaded. This extends
Stage 89's graph identity model from module roots to the complete live VM state.

## Context archive

`ScriptContext::SaveState()` accepts only the `Suspended` state, and
`LoadState()` accepts only an `Uninitialized` target. The `MASC` version 1
archive contains:

- every module image currently represented by the initial function, current
  function, or saved caller frame;
- each module's non-host global schema and live values;
- one shared object/captured-cell graph spanning globals, operand stack, locals,
  and all frames;
- the initial and current function descriptors, operand stack, current locals,
  captures, PC, and stack base;
- every caller function, return PC, locals, captures, and stack base.

Imported-function execution can therefore suspend in the provider module and
resume back into the consumer module. Module states are restored together, so a
local handle that aliases a global still addresses the same reconstructed
object.

## Stable resume points

Raw process pointers and numeric symbol IDs are never archived. Functions are
identified by module name, full declaration, and object type. A resume point
also records function code length plus the opcode and complete source location
at the next instruction. Loading resolves fresh pointers and IDs, then rejects
the archive if the target bytecode structure or source cue differs.

The archive deliberately does not save host configuration such as line
callbacks, context-pool ownership, or the script-function resolver. Those come
from the new target context and engine. Finalizer bindings, module owners, and
safe-point callbacks are rebuilt from the resolved target images.

## Transaction and safety

All module schemas, object shells, values, function targets, local layouts, and
resume markers are validated before publication. Candidate module globals and
VM vectors are cleaned on failure; the target remains uninitialized and every
published module state remains unchanged. Successful commit uses non-throwing
root swaps, then installs the suspended VM state.

The operation is single-threaded. A saved context may reference multiple
modules, but it may not span two historical images with the same module name;
there would be no unambiguous image to load after restart.

## Verification

Tests cover nested frames, a local/global object alias, closure captures,
cross-module imported calls, source-located exceptions after resume, lifecycle
preconditions, incompatible-bytecode rollback, and the official-style facade.
The MSVC and GCC suites also continue to run all Stage 89 graph and finalizer
cases.
