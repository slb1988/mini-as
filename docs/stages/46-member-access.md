# Stage 46: private and protected members

Script class fields, methods, constructors, and destructors may now be prefixed
with `private` or `protected`. Members remain public when no qualifier is
present, matching AngelScript 2.38.0. Interfaces continue to expose only public
methods and reject access qualifiers.

The type model records each member's access level and declaring class. This is
important for inherited fields: their physical slots are flattened into the
derived object layout, but their ownership does not change. A private member is
accessible only while compiling a method of its declaring class. A protected
member is also accessible from methods of derived classes. Global functions and
unrelated classes cannot access either category.

Access checking happens after overload resolution and before bytecode emission.
Constructors follow the same rules, so a protected base constructor can be
called through `super(...)`, while a private base constructor cannot. Method
visibility does not alter stable virtual slots: dispatch still uses the static
class and slot, then selects the implementation from the receiver's real
`TypeId`.

The VM needs no access-control opcode because invalid access is rejected during
module construction. Runtime field layout and call dispatch therefore remain
compact and unchanged.

This stage does not add reference casts, `final`, `abstract`, or `override`.
