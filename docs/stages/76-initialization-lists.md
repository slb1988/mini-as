# Stage 76: initialization lists

## Goal

Expressions can now use AngelScript-style brace initialization when the target
registered object exposes the container protocol:

```angelscript
array<int>@ values = {20, 21, 1};
array<int>@ empty = {};
```

Trailing commas are accepted. The target type supplies the element type, so a
brace list is deliberately not valid for `auto` without another contextual type.

## Protocol-based lowering

Initialization lists are represented by a dedicated `InitList` AST node. During
type checking, the target must be a registered host type with:

- a zero-argument factory; and
- an `insertLast(T)` method with one element parameter.

Each expression is checked and converted against `T`. This keeps the language
feature reusable by future registered containers rather than recognizing
`array` by name.

Bytecode calls the normal zero-argument host factory, duplicates the resulting
handle for every element, and invokes the ordinary `insertLast` callback. The VM
receives only existing `Dup`, `CallHost`, and `Pop` instructions; no array-specific
opcode or runtime branch was added.

## Diagnostics and verification

An untyped list reports that a target object type is required. Objects without
the factory/insertion protocol are rejected, and incompatible elements report
both the required and actual type at the element location. Candidate image
transactionality remains unchanged on any failure.

Tests cover parser shape and trailing commas, populated and empty lists, emitted
host-call bytecode, runtime element order, missing contextual types, and mixed
element diagnostics. The differential case executes the same handle-based array
initialization script through mini_angelscript and the official AngelScript
2.38.0 `scriptarray` add-on.
