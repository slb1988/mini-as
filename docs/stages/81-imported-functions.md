# Stage 81: imported functions

## Goal

Script modules can now declare a function whose implementation will be supplied
by another module after compilation:

```angelscript
import int Add(int value) from "math";

int main() {
    return Add(2);
}
```

The declaration participates in normal overload resolution and bytecode
generation, but `Build` does not silently search or merge the source module.
The host explicitly calls `BindImportedFunction` or
`BindAllImportedFunctions`, matching AngelScript's separation between
compilation and application-controlled linking.

## Module and runtime model

Each import records its typed declaration, stable declaration `FunctionId`, and
suggested source-module name. The compiled call uses
`CallableKind::ImportedFunction`. Mutable bindings live beside globals in
`ModuleState`, so binding and unbinding do not mutate the immutable
`ModuleImage`.

At execution time the VM resolves the bound target through the engine. Entering
an imported function changes the active bytecode module and global state while
retaining both module images. Every call frame saves and restores:

- bytecode and module-global state;
- immutable image ownership;
- the finalizer bytecode and state used by newly created objects;
- ordinary locals, captures, operand-stack base, and program counter.

This supports nested and circular module calls without confusing globals from
the caller and callee. Exceptions retain the failing source location and a
cross-module call stack. Calling an unbound import produces the located runtime
error `imported function is not bound`.

## Public APIs

The native module API exposes import count, declaration and source-module
inspection, individual/all binding, and individual/all unbinding. The
`mini_as::compat::ScriptModule` facade exposes the corresponding official-style
integer result-code methods.

Bindings require an exact global-function callable shape: return type,
parameters, parameter modes, and reference qualifiers must match, while manual
binding may select a differently named script or registered host function as in
the official API. Methods, other imported declarations, null pointers, and
mismatched signatures are rejected. `BindAllImportedFunctions` resolves the
same declaration name in each suggested module and does not create missing
modules.

## Bytecode and verification

Bytecode format version 3 persists imported signatures, source-module names,
and imported call descriptors. Live bindings are intentionally excluded: a
loaded module starts unbound and must be linked by its host again.

Tests cover token and AST shape, unnamed import parameters, reflection and
binding controls, wrong-signature rejection, unbound-call locations,
cross-module global state, circular imports, exception stacks, compatibility
facade result codes, and bytecode save/load followed by rebinding. The
differential case builds a separate `math` module and runs the same imported
call through mini_angelscript and official AngelScript 2.38.0.
