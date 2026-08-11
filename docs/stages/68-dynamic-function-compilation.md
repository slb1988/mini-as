# Stage 68: dynamic function compilation

`ScriptModule` can now compile exactly one additional global function without
rebuilding its original script sections:

```cpp
const mini_as::BytecodeFunction* function = module->CompileFunction(
    "console", "int evaluate() { return existing() + globalValue; }");
```

The new function is compiled against the module's complete successful-build
environment: global functions and variables, classes and interfaces, enums,
typedefs, funcdefs, and host registrations. A function added to the module is
visible to lookup and later incremental compilations. Duplicate declarations are
rejected without changing the current image.

Successful images also retain their arena-owned, type-checked definition trees.
The bytecode compiler uses those durable definitions when a new call omits an
argument, so default expressions from previously built functions are still
materialized at the new call site with their original global namespace.

Passing `false` as `addToModule` creates a detached function. The returned raw
pointer remains executable for the lifetime of its `ScriptModule`, but the
function is not visible through `GetFunctionByDecl` and cannot recursively refer
to itself. This mirrors AngelScript's distinction between compile flags `0` and
`asCOMP_ADD_TO_MODULE` while preserving the existing mini_as pointer-based API.
The optional `lineOffset` adjusts diagnostic and runtime source rows.

Each successful compilation creates a new immutable `ModuleImage`. Added images
share the existing `ModuleState`, so module globals retain identity and values.
Contexts prepared before compilation keep their old image and finish safely.
All dynamic images are retained by the module, so raw pointers returned by an
earlier incremental compilation stay valid after later compilations. They are
also registered with the engine so `ScriptContext::Prepare` can capture their
ownership.

Incremental compilation rebuilds the callable descriptor table carefully. Old
descriptors remain at their original indices for copied bytecode, while new
function instructions are rebased to descriptors appended after them. This is
required for existing functions, new functions, closures, delegates, host calls,
and virtual calls to coexist in one image.

Tests cover calls into existing globals, default arguments, functions,
constructors, methods, and a previously added dynamic function; detached
execution and lookup isolation; old Context snapshots; duplicate,
recursive-detached, and multi-declaration errors; and line-offset runtime
exception locations. The differential runner exercises both compile modes
against the exact AngelScript `v2.38.0` source in `D:\Github\angelscript2`.

Dynamic removal is deliberately left to the next independent feature commit.
