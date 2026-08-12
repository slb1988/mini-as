# Stage 83: external shared script entities

## Goal

A module can reference an already compiled shared class, interface, enum,
funcdef, or global function without repeating its implementation:

```angelscript
external shared class Counter;
external shared interface ICounter;
external shared int Twice(int value);
```

`shared external` is accepted as the equivalent spelling. Both words remain
contextual identifiers. An external declaration must be `shared`, must end in
a semicolon, and must match an entity previously published by the same engine.

## Compilation and execution

Successful shared builds publish canonical signatures and retain the owning
definition tree. A later external build injects only the requested canonical
signatures into type checking. The retained tree supplies constructor default
arguments and field initializers, but its function bodies are not copied into
the consumer module.

External calls use `CallableKind::ExternalFunction` and the shared entity's
engine-wide stable `FunctionId`. At runtime the existing script-function
resolver selects the defining immutable `ModuleImage`, including its global
state, ownership, finalizer state, and source locations. Virtual calls and
destructors use the same resolver fallback when their implementation is not in
the consumer bytecode.

No import binding step is needed. Missing or mismatched prior definitions are
compile-time errors, while an unavailable implementation in a stale runtime
environment is a located runtime exception.

## Bytecode and verification

Bytecode format version 5 persists external flags on signatures and AST nodes,
and accepts external callable descriptors. Loading a defining module rebuilds
the engine's canonical shared registry; a loaded consumer continues to resolve
external calls by stable ID.

Tests cover both modifier orders, all supported entity kinds, missing-definition
diagnostics, constructors with default arguments, field initializers, interface
dispatch, direct calls, defining-module exception locations, and two-module
bytecode save/load. The differential case executes the same external consumer
against mini_angelscript and AngelScript 2.38.0.
