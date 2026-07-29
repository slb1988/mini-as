# 13 - Script classes, fields, handles, and interfaces

Script class metadata is built before function checking. Fields receive stable
indexes and become a contiguous `Value` array after the intrusive object header.
`NEW_OBJECT` invokes the synthesized default factory, while `LOAD_FIELD` and
`STORE_FIELD` use those compile-time indexes and trap on null handles.

Objects cross contexts through the same `ObjectHandle` used for host objects.
There is no special lifetime path for script instances: local assignment,
return, and host retention all apply ordinary AddRef/Release semantics.

Interfaces are intentionally minimal. Build verifies that a concrete class has
each required signature and creates an interface-to-qualified-method table.
Inheritance and dynamic method-call bytecode remain outside this teaching
subset. Tests cover field layout and execution, cross-context handles,
interface table lookup, and missing-method diagnostics.
