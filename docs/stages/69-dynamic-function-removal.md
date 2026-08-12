# Stage 69: dynamic function removal

Global script functions can now be removed from a module's active scope:

```cpp
const mini_as::BytecodeFunction* function =
    module->GetFunctionByDecl("int evaluate()");

bool removed = module->RemoveFunction(function);
```

Removal is a visibility operation, not immediate destruction. The next immutable
`ModuleImage` records the removed `FunctionId` and erases its signature from the
incremental compilation environment. Module lookup and function-metadata lookup
therefore stop returning it, and newly compiled code cannot resolve its name.

The executable bytecode stays in the image. Existing functions keep their old
call descriptors and continue resolving the removed id, and the prior image is
retained so a raw function pointer obtained before removal remains safe to pass
to `ScriptContext::Prepare`. A later incremental compilation carries hidden
functions forward even though they are absent from name resolution.

After removal, a new function with the same declaration may be compiled. It gets
a new stable id: existing callers continue invoking the retired implementation,
while new callers resolve the replacement. This matches AngelScript 2.38.0's
documented incremental-build behavior.

Only visible module-level script functions are removable. Null pointers,
detached functions, methods, host functions, and already removed functions are
rejected without publishing a new image. Script classes and their finalizers are
unaffected.

Tests cover lookup isolation, old pointer execution, an existing caller's hidden
dependency, failed new compilation against a removed name, same-declaration
replacement, new-call binding, repeated removal, and invalid target categories.
The differential runner performs added-function removal with the exact
AngelScript `v2.38.0` source from `D:\Github\angelscript2`.
