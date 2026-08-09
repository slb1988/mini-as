# Stage 56: weak references

Scripts can now use the official weak-reference add-on surface with script
classes and interfaces:

```angelscript
Payload@ object = Payload(42);
weakref<Payload> reference(object);

Payload@ locked = reference.get();
@object = null;
@locked = null;

bool expired = reference.get() is null;
```

`weakref<T>` and `const_weakref<T>` are parsed as dedicated parameterized value
types. This stage implements them intrinsically so weak references can arrive
before the general registered-template system planned for v0.6. The accepted
script spelling, default and explicit constructors, `get()`, implicit locking,
copying, equality, and explicit handle assignment (`@reference = object`) match
the 2.38.0 add-on surface. Template arguments must name a script reference type
and must not include `@`.

Every `RefObject` owns a shared lifetime token. A `WeakObjectHandle` stores only
the raw identity and token, never an owning `ObjectHandle`. The token mutex
serializes the final strong `Release` with weak locking: either the lock first
increments the strong count, or destruction first marks the token dead. This
prevents a weak reference from resurrecting an object after its count reaches
zero. The token also distinguishes a destroyed allocation from a later object
that happens to reuse the same address.

The bytecode operations are deliberately small:

- `MakeWeakRef` converts a compatible object handle into a non-owning value;
- `LockWeakRef` atomically returns a strong handle or null;
- `ToConstWeakRef` preserves the lifetime token while changing weakref type.

Weak values are supported in locals, globals, fields, parameters, returns, and
captured cells. Object reference enumeration ignores them, so a weak self-link
or graph edge does not become a GC root. They remain valid values after the
object dies and then consistently lock to null.

The differential runner now registers AngelScript 2.38.0's official `weakref`
add-on and compares an unchanged direct-construction/lifetime script. Unit tests
also cover AST types, implicit locking, bytecode, strong-lock lifetime, expired
access, invalid subtype diagnostics, cross-type assignment, and GC behavior.

Two boundaries remain explicit. Registered host types do not gain weakref
behaviours until the host-registration roadmap, and mini_as does not yet model
const-qualified object handles, so `const_weakref<T>` preserves weak lifetime
and type identity but cannot enforce read-only access to the locked object.
