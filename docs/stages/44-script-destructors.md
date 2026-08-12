# Stage 44: script destructors

Script classes can now declare one parameterless destructor with
`~ClassName() { ... }`. The parser keeps destructors distinct from ordinary
methods, the type checker rejects mismatched names, parameters, missing bodies,
duplicates, and interface destructors, and bytecode links each destructor with
stable `TypeId` and `FunctionId` values.

Releasing the last object handle never enters the VM directly. Instead,
`ScriptObject` marks the destructor as scheduled, retains one queue reference,
and submits itself to the engine finalizer queue. The VM drains that queue only
at instruction boundaries, so a callee's local objects are finalized before
the caller executes its next instruction without re-entering the VM from
`Release()`. The scheduled flag guarantees exactly-once execution, including
objects found by cycle collection.

Each queued object retains an immutable bytecode copy containing its destructor
and other script call targets. Module state is observed weakly to avoid a cycle
between module globals and their own finalizer metadata. This keeps host calls
and ordinary script calls available to destructors while a module image is
live. A destructor exception is reported through the engine diagnostic callback
with the failing source location and does not replace the result of the script
that triggered finalization.

The current collector clears object-handle fields before queued destructors for
an unreachable cycle run. Scalar fields remain available, but observing other
members of the collected cycle is intentionally unspecified at this stage.
