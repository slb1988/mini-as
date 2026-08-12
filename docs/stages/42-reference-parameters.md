# Stage 42: in, out, and inout parameters

Function and method parameters now accept AngelScript's `&in`, `&out`, and
`&inout` direction qualifiers. An unqualified `&` is treated as `&inout`.
Parameter modes are durable `FunctionSignature` metadata and participate in
declaration strings, bytecode lookup, virtual calls, and host registration.

The teaching VM keeps references out of `Value`. Calls use copy-in/copy-out:

1. `in` and `inout` parameters copy the caller's current value into the callee;
   `out` parameters start with the type's default value.
2. The callee executes with ordinary typed local slots. `in` slots are
   read-only during type checking.
3. A successful return publishes changed `out` and `inout` slots after the
   normal return value. The caller writes them back through its `LValueRef`.

Local, module-global, and object-field targets are supported. Output targets
must be mutable lvalues of the exact declared type. A runtime exception skips
copy-out, so partially modified callee slots never leak into the caller.

`GenericCall` exposes matching `SetArg*` methods. A host callback implements an
output parameter by replacing its argument slot; the VM performs the same
copy-out sequence used by script functions.

The AngelScript 2.38.0 differential runner enables
`asEP_ALLOW_UNSAFE_REFERENCES`, which is required by the official engine for
primitive `&inout` parameters.
