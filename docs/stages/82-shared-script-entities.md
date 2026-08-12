# Stage 82: shared script entities

## Goal

Classes, interfaces, enums, funcdefs, and global functions can now be declared
`shared` in multiple modules:

```angelscript
shared interface ICounter { int read(); }
shared class Counter : ICounter {
    int value;
    Counter(int start) { value = start; }
    int read() { return value; }
}
shared int Twice(int value) { return value * 2; }
```

Matching declarations receive the same engine-wide type and function IDs.
Objects can therefore cross an imported-function boundary and be consumed as
the same shared class or interface in another module.

`shared` is parsed contextually at the top level. This follows official 2.38.0
behavior and preserves existing scripts that use `shared` as a variable name.

## Definition registry

Every successful build publishes a structural fingerprint for each shared
entity. The fingerprint includes entity kind, qualified name, type and
reference qualifiers, members, operators, literals, and function bodies while
excluding source locations and whitespace. A later module may repeat the same
definition, including overloads, but a different field layout, declaration, or
body is rejected before any new image or metadata is published.

Publication is transactional: failed type checking, fingerprint validation,
bytecode compilation, or global initialization leaves both the previous module
image and the engine's shared registry unchanged.

Shared global functions and methods use engine-wide stable `FunctionId` keys.
Non-shared functions keep their module-qualified IDs. Shared object types reuse
the engine's stable `TypeInfo`, and reflection exposes the shared flag.

## Isolation rules

Shared code may depend on primitives, built-in strings, host-registered types
and functions, and other shared script entities. The type checker rejects:

- non-shared field, parameter, return, local, inferred, or funcdef types;
- inheritance from a non-shared script type;
- reads or writes of module-global variables;
- calls or function handles targeting non-shared script functions.

Import binding also rejects object, enum, funcdef, or weak-reference types that
are neither host-registered nor shared. Matching names in two modules are not
enough to make ordinary module-local types interchangeable.

## Bytecode and verification

Bytecode format version 4 persists shared flags on functions, classes, enums,
funcdefs, and syntax-tree nodes. Loading reconstructs and validates shared
fingerprints, remaps shared functions to engine-wide IDs, and still preserves
the rule that live import bindings must be restored by the host.

Tests cover every supported shared entity kind, cross-module class/interface
identity, stable function IDs, imported object exchange, mismatched-definition
diagnostics, isolation diagnostics, rejection of non-shared import types,
located runtime failures, and two-module bytecode save/load. The differential
case runs matching shared definitions and an imported shared object through
mini_angelscript and official AngelScript 2.38.0.

The shorter `external shared` declarations are deliberately the next stage;
this stage requires each participating module to provide the matching full
definition.
