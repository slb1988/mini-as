# Stage 38: Typedefs

`typedef <primitive> <name>;` introduces a module-level alias for a built-in
primitive type, matching AngelScript 2.38.0's script typedef restriction.
Aliases may be used in global, local, field, parameter, and return types,
including declarations that appear before the typedef in the combined module.

The parser discovers aliases before building declaration nodes and resolves
them to their canonical storage type. The type checker retains a separate
`TypedefSignature` with a stable `TypeId` for later reflection work while the
current bytecode and `Value` representation continue to use the underlying
primitive directly. Duplicate alias names and non-primitive source types are
compile diagnostics.
