# Stage 47: reference casts

The `cast<T>(expression)` operator now performs explicit AngelScript reference
casts between script object handles. The target `T` must name a script class or
interface; the expression must produce an object handle. The result is always a
handle to `T`.

Unlike implicit handle conversion, which remains limited to derived-to-base and
class-to-interface directions, an explicit reference cast may be attempted
between any script object handle types. The VM evaluates the receiver's real
`TypeInfo`: class targets use the base chain and interface targets use the
implemented-interface table. A successful cast keeps the same `ObjectHandle`,
so identity and lifetime are preserved. An incompatible object or null source
produces a null handle without raising an exception.

Bytecode uses `CastObject` with a stable `TypeId`; it never stores a movable
metadata pointer. Module images now expose both class and interface `TypeInfo`
entries to the VM so the opcode can resolve either target kind. If a null cast
result is subsequently dereferenced, the existing call or field opcode reports
the exception at that use site.

This stage covers built-in script hierarchy casts only. User-defined `opCast`
and `opImplCast` overloads arrive with the separate operator-overload feature.
