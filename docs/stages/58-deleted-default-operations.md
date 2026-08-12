# Stage 58: deleted default operations

Script classes can now disable AngelScript's three compiler-provided object
operations with the official trailing `delete` attribute:

```angelscript
class Locked {
    Locked() delete;
    Locked(const Locked &in other) delete;
    Locked &opAssign(const Locked &in other) delete;
}
```

`delete` remains a contextual function attribute, as it is in AngelScript
2.38.0, rather than becoming a globally reserved word. The parser also accepts
the `const Type &in` parameter spelling needed by the official copy signatures
and records constness on the parameter AST node.

The type checker classifies deleted declarations before publishing callable
methods. `ClassSignature` independently records deletion of default
construction, generated copy construction, and generated copy assignment.
Deleted declarations therefore never receive a `FunctionId`, bytecode body, or
virtual slot. A deleted copy constructor also suppresses the Stage 57 generated
copy path.

Calls to a deleted default constructor, implicit use of a deleted base default
constructor, generated copies after copy deletion, and value assignments after
`opAssign` deletion are compile-time errors. Explicit constructors with other
signatures remain usable. Explicit handle assignment (`@left = right`) remains
legal because it rebinds a handle rather than copying object state.

Only the three automatic operations may be deleted. A deleted declaration with
a body, repeated deletion, or a simultaneous explicit definition is rejected.
The differential case uses all three official declarations unchanged and
checks alternate construction plus explicit handle rebinding against 2.38.0.

The teaching runtime still lacks generated member-wise `opAssign`; ordinary
object assignment without an overload retains its current handle-backed model.
This stage makes deletion constraints accurate without claiming full
AngelScript value-object storage semantics.
