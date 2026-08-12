# Stage 72: official-style engine, module, and context facade

## Goal

This stage adds `include/mini_as/compat.hpp` and the `mini_as::compat`
namespace. It lets embedding code use AngelScript-shaped integer results,
module flags, compile flags, and execution states without changing the existing
RAII-oriented `mini_as::ScriptEngine`, `ScriptModule`, or `ScriptContext` API.

The numeric constants are taken from the local AngelScript 2.38.0
`angelscript.h` tag. For example, success is `0`, invalid argument is `-5`,
finished execution is `0`, suspended execution is `1`, and always-create module
lookup is `2`.

## Ownership model

The facade deliberately keeps C++ ownership explicit:

```cpp
auto engine = mini_as::compat::CreateScriptEngine();
auto* module = engine->GetModule("game", mini_as::compat::asGM_ALWAYS_CREATE);
auto context = engine->CreateContext();
```

The engine owns module wrappers and `CreateContext` returns `unique_ptr`. This
does not emulate official SDK `AddRef`/`Release`, ABI, headers, native calling
conventions, or interface vtables. `Native()` is available on all three wrappers
when an embedding needs a mini-specific feature not yet represented by the
facade.

## Initial surface

`compat::ScriptEngine` provides message callbacks, generic global function and
property registration, enum/typedef/funcdef registration, module lookup flags,
and context creation.

`compat::ScriptModule` provides official-shaped section addition, build,
function lookup, dynamic compilation with `asCOMP_ADD_TO_MODULE`, removal, and
bytecode save/load. `lineOffset` is implemented by prefixing logical newlines so
diagnostic and instruction rows retain the requested offset.

`compat::ScriptContext` provides integer-returning prepare/argument/execute
operations, official execution-state numbers, primitive return accessors,
exception text, suspension/abort, line callbacks, and stack/function/line/local
debug queries. Debug values are still safe `Value` snapshots rather than raw VM
addresses.

Wrapper validation maps null functions, bad declarations, invalid flags, bad
argument indices, and unavailable stack levels to the closest 2.38.0 result
code. A failed native compiler/build operation currently maps to the generic
`asERROR`; richer diagnostic-to-result classification can be added without
changing the native API.

## Verification

Tests verify:

- the complete 2.38.0 result-code range plus execution/module/compile constants;
- create/find/replace module policy and wrapper identity;
- generic host registration, section line offsets, build, argument setting,
  execution, return values, and invalid-state results;
- dynamic compilation, removal, versioned bytecode save/load, and debug queries;
- a differential script executed through `mini_as::compat` and the official
  AngelScript 2.38.0 engine with identical normalized output.

The next host-control stage adds namespaces, access masks, and configuration
groups. The v0.5 embedding matrix will distinguish facade-shaped operations
from true SDK source or ABI compatibility.
