# Stage 80: any and ref add-ons

## Goal

The add-on library now provides two small type-erasure tools used by ordinary
AngelScript embedding code:

```angelscript
any@ box = any(int64(40));
int64 answer;
box.retrieve(answer);

ref first;
ref second;
bool bothNull = first == second && first.isNull();
```

`any` is a garbage-collected reference object containing one copied `Value`.
`ref` is a registered value type containing an arbitrary object handle. They
remain add-ons rather than VM primitives and therefore compile to the existing
factory and host-method call instructions.

## Registered surface

`RegisterScriptAny` registers factories plus `store` and `retrieve` overloads
for `int64`, `double`, the current built-in `string`, `bool`, and `ref` when the
ref add-on is already registered. It also provides `hasValue`, `typeName`, and
`clear`. Numeric retrieval performs the same checked conversions as other
host-facing values; a type mismatch returns `false` without modifying the out
argument.

`RegisterScriptRef` registers default/copy value semantics, `opEquals`,
`isNull`, and `typeName`. Embedders can bridge any registered object handle
through `MakeScriptRef` and `GetScriptRef`. The official wildcard constructor
and generic cast surface depends on wildcard parameters and is intentionally
deferred to the variadic-argument stage; unsupported script calls currently
produce normal overload diagnostics.

When both add-ons are used, register `ref` first so `any` can publish its ref
overloads.

## Runtime and GC

Both add-ons reuse the general `Value` reference traversal introduced before
this stage. `ScriptAny` enumerates and clears references held by its payload.
The ref value uses `Value::ManagedHostValue` callbacks, so a ref stored in a
script field, array, dictionary, or any participates in cycle detection even
though it is represented by host value storage rather than a direct
`ObjectHandle` variant alternative.

This keeps reference ownership explicit and lets the collector break cycles
without teaching it about each add-on type.

## Verification

Tests cover factory construction, typed store/retrieve and out writeback,
default-ref equality, C++ object-handle bridging, type names, invalid overload
diagnostics, direct and script-field reference cycles, host-call emission, and
bytecode save/load with add-on symbol rebinding.

The differential case runs the same numeric `any` and null `ref` script through
mini_angelscript and the official AngelScript 2.38.0 scriptany and scripthandle
add-ons.
