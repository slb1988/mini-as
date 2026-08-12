# Stage 60: registered reference type factories and behaviours

Registered host reference types can now be constructed by unchanged script
syntax after the host supplies one or more factory overloads:

```cpp
const mini_as::TypeInfo* type = engine->RegisterObjectType("HostRef");
engine->RegisterObjectFactory(
    "HostRef", "HostRef@ f(int value)",
    [type](mini_as::GenericCall& call) {
        call.SetReturnObject(mini_as::ObjectHandle(
            new HostRef(type, call.GetArgInt(0))));
    });
```

```angelscript
HostRef@ value = HostRef(42);
```

This follows the AngelScript 2.38.0 [basic reference type
contract](https://www.angelcode.com/angelscript/sdk/docs/manual/doc_reg_basicref.html):
a constructible reference type has a factory plus add-reference and release
behaviours. `mini_as` keeps the portable `GenericCall` boundary for factories.
Its add-reference and release behaviours are deliberately intrinsic: registered
objects derive from `RefObject`, and every `ObjectHandle` copy, move, and
destruction applies the existing atomic `AddRef`/`Release` implementation.
There is no native calling convention or user callback hidden inside handle
bookkeeping.

Factory declarations use the official `Type@ f(...)` spelling. Overloads are
selected with the normal argument conversion and named-argument rules, but the
identifier `f` is not published as a global script function. Internally each
factory has a stable `FunctionId` and a factory-qualified durable key, so an
ordinary host function with the same declaration cannot collide with it.

Registered reference types are predeclared as host `ClassSignature` entries.
The type checker resolves `Type(args)` only against their factories. The
bytecode compiler emits a linked `CallHost` descriptor directly; it never emits
`NewObject`, because host code owns allocation and initialization. A registered
type without a factory remains intentionally uninstantiable, matching the
official singleton or object-pool use case.

The VM requires a non-null object of the declared type. A factory may fail by
setting a script exception; returning null without an exception is rejected at
the construction source location, as required by the official contract.
Objects produced successfully then follow deterministic `ObjectHandle`
lifetime management.

This stage covers basic, reference-counted host types. Host object methods,
properties, value types, garbage-collected host object behaviours, and native
ABI calling conventions remain separate roadmap items. The differential case
registers the same `HostRef@ f(int)` factory and unchanged construction script
in mini_as and AngelScript 2.38.0.
