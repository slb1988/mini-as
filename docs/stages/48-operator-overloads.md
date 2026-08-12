# Stage 48: operator overloads

Script classes can now define AngelScript's core operator methods. Arithmetic,
bitwise, shift, and power expressions select `opAdd` through `opUShr`, with the
corresponding `_r` methods considered when the object is on the right. Unary
negation and complement use `opNeg` and `opCom`; prefix and postfix increments
and decrements use their four dedicated methods. Assignment expressions cover
`opAssign` and every compound-assignment variant.

Equality first looks for a bool-returning `opEquals`, then falls back to an
int-returning `opCmp`. Ordering also uses `opCmp`, including the reversed
zero-comparison rule when the object is the right operand. The `is` operator is
deliberately never overloaded: it continues to compare handle identity.
Callable objects lower `value(args)` to `value.opCall(args)`.

Explicit `cast<T>` may use `opCast` or `opImplCast`. Primitive-style casts such
as `int(value)` and class-style conversions such as `Label(value)` select
`opConv` or `opImplConv` by the requested return type. Conversion methods are
the one method family that may overload on return type alone. Implicit
`opImplConv` and `opImplCast` conversions are available at the common
declaration, assignment, argument, and return conversion points. The bytecode
compiler consumes the already-evaluated receiver and emits one virtual call,
so a converting expression is never evaluated twice.

All operator calls use the existing stable virtual layout and `CallableRef`
descriptors. AST nodes retain only the selected method name and whether the
right-hand form won overload resolution; bytecode contains stable `TypeId` and
virtual-slot data rather than metadata pointers. A null receiver reports the
operator expression's source location.

The differential corpus exercises direct and reverse arithmetic, `opEquals`
and `opCmp`, identity, assignment, increment, `opCall`, and explicit and
implicit conversions against AngelScript 2.38.0. `opIndex` remains with the
planned indexing feature, while property accessors are the next class-language
stage. The current teaching runtime still models script class values as
reference-counted handles rather than implementing AngelScript's complete
value-object copy model.
