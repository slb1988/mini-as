# Stage 77: indexing expressions

## Goal

Postfix indexing is now a first-class expression and lvalue:

```angelscript
int old = values[next()]++;
values[next()] += 9;
values[1] = 10;
return values[0];
```

Bracket tokens and the `Index` AST node were appended to their enums so existing
version-2 bytecode token/node values remain stable.

## Registered index protocol

The type checker resolves indexing through two registered methods:

- `T get(uint index) const` for reads;
- `T set(uint index, T value)` for writes.

The setter returns the stored value so assignment expressions retain their
ordinary result. The array add-on exposes this protocol, but the parser,
type-checker, and compiler do not recognize the array type name.

Non-indexable receivers, incompatible index types, missing setters, and
inconsistent getter/setter element types are compile errors. Runtime bounds
checks remain the add-on's responsibility and preserve the bracket expression's
source location.

## Lvalue lowering

`LValueRef::Index` is now fully implemented. Simple reads and writes emit normal
host method calls. Compound assignment and prefix/postfix increment cache the
receiver and converted index in hidden locals before invoking `get` and `set`.
This guarantees that expressions such as `values[next()]++` call `next()` once,
while preserving prefix/postfix result semantics.

No array-specific opcode was introduced. The emitted program uses existing
local, arithmetic, and `CallHost` instructions, keeping indexing available to
future registered types that implement the same protocol.

## Verification

Tests cover tokenization, chained-index AST shape, read/write expressions,
assignment results, compound updates, prefix/postfix increments, single
evaluation of side-effecting indices, invalid receiver diagnostics, emitted
host calls, and located bounds exceptions.

The differential case runs the same initialized array script through this
protocol and the official AngelScript 2.38.0 array `opIndex` implementation.
