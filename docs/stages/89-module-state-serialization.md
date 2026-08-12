# Stage 89: module globals and object-graph serialization

## Goal

Persist a built module's live script-global state independently from its
program image. `SaveBytecode()` still archives code and metadata; the new
`SaveState()` / `LoadState()` pair archives mutable values and the strongly
reachable object graph. A fresh engine can therefore load bytecode first and
then restore a saved world state.

## Archive model

State archives use the `MASS` magic, format version 1, a bounded payload, and an
FNV-1a checksum. The payload contains:

1. the module name and ordered non-host global schema;
2. object shell descriptors and captured-cell count;
3. global root values;
4. object payloads and captured-cell payloads.

Objects and captured cells receive one-based archive IDs. Shells are allocated
before values are decoded, so forward references, shared aliases, closures, and
cycles reconstruct without recursive construction. Weak references record an
ID only when their target is also strongly reachable; a weak-only target is not
promoted into the saved graph.

Function handles use stable module name, declaration, object type, funcdef name,
and dispatch-type spelling rather than process-local numeric IDs. Loading after
`LoadBytecode()` resolves these descriptors against the target engine's newly
assigned IDs.

## Supported graph nodes

The built-in codec handles:

- scalar, enum, string, object, weak-reference, and function-handle values;
- anonymous-function captured cells;
- script objects, including inheritance-flattened fields and destructors;
- the standard `array`, `dictionary`, `any`, `ref`, and `dictionaryValue`
  representations delivered in earlier v0.6 stages.

Arbitrary registered reference and value types are rejected with a precise
diagnostic until the host-serializer registration surface is introduced. A
silent pointer dump would not be portable or safe.

## Transaction and ownership rules

Only script globals are archived. Registered global properties remain owned by
the host and keep their current value during restore.

Loading validates the complete module/global/object schema and decodes into a
candidate root vector. Any checksum, bounds, type, symbol, or object-codec error
breaks candidate references and leaves the published module state unchanged.
After successful validation, restored script objects receive the module's
finalizer binding, roots are swapped atomically, and finalizers released by the
old ref-counted graph drain at the normal engine safe point. Unreachable cycles
remain the GC's responsibility.

The operation is single-threaded. Hosts must not execute or resume a context
against the same `ModuleState` concurrently with `LoadState()`.

## Verification

Tests cover cross-engine restoration after bytecode ID remapping, primitive
globals, aliases and cyclic script objects, weak references, closures, standard
add-on containers, external host-property exclusion, checksum and schema
rollback, unsupported host-object diagnostics, restored destructor lifetime,
and the official-style compatibility facade.
