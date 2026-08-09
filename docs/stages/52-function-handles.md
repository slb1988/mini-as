# Stage 52: function handles

`funcdef` types can now be used as first-class function handles. A script can
take the address of a matching global function with `@function`, store that
handle in a local, module global, object field, parameter, or return value, and
invoke it with the ordinary call syntax. Matching registered host functions
are supported through the same `GenericCall` bridge.

Function handles are typed by the complete funcdef signature: return type and
reference qualification, parameter types, and `in`, `out`, or `inout` modes.
Overloaded global functions are resolved from the expected funcdef type during
initialization, assignment, and return checking. As in AngelScript 2.38.0,
replacing an existing handle uses an explicit handle target, for example
`@callback = @multiply`. Null handles can be default-initialized, assigned from
`null`, and compared with `is`; invoking one raises a located runtime exception.

The runtime value contains a stable `FunctionId`, the funcdef's stable
`TypeId`, and a script/host discriminator. `CallHandle` reads this dynamic
target while its `CallableRef` supplies the statically checked parameter count
and funcdef identity. Script calls enter the normal VM frame machinery, while
host calls retain return-value and output-parameter validation. No bytecode or
runtime structure stores a function pointer into a movable container.

Prepared contexts continue to own their immutable module image, so handles to
script functions remain valid for that image while a module is rebuilt. A
failed rebuild still preserves the prior image and its global handle state.

This stage intentionally supports global script and registered host functions.
Binding an instance method requires an object plus a method and is the separate
delegate feature in Stage 53. Anonymous functions, captured locals, child
funcdefs, and public host-side function-handle reflection remain later roadmap
items.

Tests cover parser/type shape, signature mismatch diagnostics, local/global/
field storage, explicit reassignment, parameter passing, script and host
indirect calls, the `CallHandle` opcode, null identity, and null-call exception
location. The differential case executes the same callback script with
mini_angelscript and AngelScript 2.38.0 and returns `42` on both.
