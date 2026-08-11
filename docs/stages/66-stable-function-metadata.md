# Stage 66: stable function reflection metadata

The engine now exposes durable reflection records for host and script callables:

```cpp
const mini_as::FunctionMetadata* function =
    module->GetFunctionMetadataByDecl("int answer(int)");

function = engine->GetFunctionMetadataById(function->id);
function = engine->GetFunctionMetadataByIndex(0);
```

`FunctionMetadata` carries the stable `FunctionId`, owning module name, and the
complete `FunctionSignature`. The signature distinguishes global functions,
methods, constructors, destructors, factories, host callbacks, parameter modes,
defaults, and reference returns. Records live in engine-owned `std::deque`
storage. Rebuilding a function refreshes the existing record in place, so a
cached metadata pointer and its id stay valid.

Host global functions, object factories, and object methods publish as soon as
their registration succeeds. Script global functions and class callables publish
only after parsing, type checking, bytecode generation, linking, and module-global
initialization all succeed. A failed rebuild therefore cannot expose rejected
signatures or mutate the last successful reflected view.

`ScriptModule::GetFunctionMetadataByDecl` is the reflection counterpart of the
existing `GetFunctionByDecl` convenience API. It resolves through the current
immutable module image, then returns the engine-owned stable record. Existing
bytecode-function APIs remain source compatible.

AngelScript 2.38.0 exposes global functions through
`GetGlobalFunctionByIndex`, `GetGlobalFunctionByDecl`, and `asIScriptFunction`.
mini_as deliberately uses a compact value API instead of cloning that interface;
the later `mini_as::compat` facade can translate official-style calls to this
catalog. Tests compare registered global lookup with the exact `v2.38.0` source
from `D:\Github\angelscript2`.

Removal and retirement semantics are intentionally deferred to the planned
dynamic function-removal feature. This stage establishes stable publication,
lookup, and rebuild behavior without changing module lifetime rules.
