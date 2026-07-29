# 12 - Reference objects and deterministic lifetime

`RefObject` is the portable host protocol: an atomic intrusive count, stable
`TypeInfo`, and a virtual reference enumerator reserved for GC. `ObjectHandle`
is a value object whose copy, move, assignment, and destruction translate into
the exact AddRef/Release operations required by that protocol.

Because `Value` stores `ObjectHandle`, the same lifetime rules automatically
cover constants, locals, arguments, return values, operand stack temporaries,
and GenericCall. Replacing a local or unwinding a context releases the old
object without a separate cleanup opcode in this simplified VM.

Tests verify reference counts at each scope boundary, object round-tripping
through script and host calls, deterministic destruction, and runtime rejection
of an object whose registered type differs from the function parameter.
