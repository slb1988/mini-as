# Stage 43: return references

Script functions and methods can now declare mutable or read-only return
references with `int &access()` and `const int &read()`.

The compiler accepts references to module globals, fields reached through
module globals, and fields of the current method receiver. References to local
variables, parameters, and fields reached only through local objects are
rejected because their storage may disappear when the function returns.

The VM does not expose a native pointer. `MAKE_GLOBAL_REF` and
`MAKE_FIELD_REF` create a `ReferenceStorage` descriptor containing a stable
global id or a retained object handle plus field slot. Ordinary expression
calls emit `LOAD_REF`; assignments and compound assignments retain the
descriptor and emit `STORE_REF`. Prefix/postfix increment use the same dynamic
lvalue path. Holding the object handle keeps a returned
field reference alive across the call boundary.

Read-only return references participate in lvalue checking, so they can be read
normally but cannot be assignment targets. Runtime failures, such as forming a
field reference through a null global handle, report the return statement's
source location.

Host return references remain rejected until registered host properties provide
a stable storage abstraction. This avoids accepting declarations that
`GenericCall` cannot safely implement without exposing C++ addresses.
