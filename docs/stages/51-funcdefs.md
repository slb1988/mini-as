# Stage 51: funcdef declarations

Scripts can now declare global and namespace-scoped function signatures with
AngelScript's `funcdef` syntax. Return values, const or mutable return
references, primitive and object parameters, and `in`, `out`, and `inout`
parameter modes are preserved. Parameter names are optional, matching official
declarations such as `funcdef bool Filter(int, int);`.

The parser represents a funcdef as a dedicated declaration rather than a
body-less executable function. The type checker publishes a `FuncdefSignature`
containing the canonical `FunctionSignature` and a stable `TypeId`. Duplicate
funcdef names, collisions with current script type declarations, and default
arguments are rejected. Names declared inside namespaces retain their fully
qualified identity.

Successful module construction copies immutable funcdef metadata into the
`BytecodeModule`. The engine assigns IDs through the same stable type registry
used for classes, interfaces, enums, and typedefs, so rebuilding a module does
not change the identity of an unchanged funcdef. No callable bytecode function
is emitted for the declaration itself.

This stage deliberately establishes the function-type description only.
Variables and parameters that hold funcdef handles, `@function` expressions,
indirect calls, and null-handle runtime behavior arrive in the next function
handle feature. Child funcdefs remain a later, separate roadmap item.

Tests cover tokenizer/parser shape, optional parameter names, reference modes,
namespace metadata, stable IDs, duplicate/conflict/default diagnostics, and an
unchanged differential script accepted by AngelScript 2.38.0.
