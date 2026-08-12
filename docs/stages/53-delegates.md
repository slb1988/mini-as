# Stage 53: delegates

Funcdef construction now binds an object instance and one of its methods into a
delegate, using AngelScript's `Callback(object.method)` syntax. The resulting
value is the same funcdef handle type introduced in Stage 52 and can be stored,
passed, reassigned, compared, and invoked through the existing `CallHandle`
path.

Delegate type checking requires an exact funcdef match across return type,
return-reference qualifiers, parameter types, and parameter modes. Overloaded
methods are selected from that signature, normal member access rules apply, and
malformed construction or mismatched methods produce compile diagnostics.

`MakeDelegate` evaluates the receiver once and packages it with the funcdef's
stable `TypeId` and a virtual call descriptor. The descriptor records the
static receiver type and stable virtual slot rather than a movable method
pointer. At invocation, the VM resolves the implementation from the receiver's
real `TypeInfo`, so a delegate built from a base or interface handle still
calls an override on the concrete object.

The embedded `ObjectHandle` is a strong reference, matching official delegate
lifetime behavior. A delegate remains callable after the original local object
handle leaves scope. Delegate receivers also participate in reference
enumeration and clearing, allowing the existing collector to reclaim cycles
such as an object field that stores a delegate back to the same object. Creating
a delegate from a null receiver raises a located runtime exception before a
partially usable handle is published.

This stage binds script instance methods. Registered object methods arrive with
the v0.5 host object registration features. Weakly bound callbacks are deferred
to weak references, and anonymous functions with captures remain the next v0.4
feature.

Tests cover exact signature diagnostics, emitted `MakeDelegate` bytecode,
stateful calls, virtual override dispatch, receiver lifetime, null-receiver
location, delegate GC cycles, and a differential script that returns `52` in
both mini_angelscript and AngelScript 2.38.0.
