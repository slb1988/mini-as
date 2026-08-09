# Stage 49: property accessors

Script classes and interfaces can now expose virtual properties. Both official
AngelScript declaration forms are accepted: compact declarations such as
`int value { get const; set; }`, and explicitly named `get_value` / `set_value`
methods decorated with `property`. A method whose name merely starts with
`get_` or `set_` is not treated as a property unless it carries the decorator.

The parser expands compact declarations into ordinary accessor method ASTs.
The setter receives the implicit parameter named `value`. Type metadata marks
accessor methods so inherited and interface methods keep the same stable
virtual slots as normal methods. The type checker requires a getter to return a
non-void value with no parameters and a setter to return void with one
parameter; paired types must match. Indexed accessors are diagnosed here and
remain assigned to the later indexing feature.

Reading `object.value` lowers to `object.get_value()`, while assignment lowers
to `object.set_value(result)`. This also works inside class methods when `this.`
is omitted. Read-only, write-only, malformed, unmarked, and inaccessible
properties receive distinct build diagnostics. Interface-typed handles use
the existing `CallVirtual` dispatch path, so an implementation override is
selected from the receiver's real `TypeId`.

Compound assignment evaluates the receiver once, stores the handle in a hidden
local, calls the getter, computes the new numeric or string value, and then
calls the setter. The assigned value remains the expression result. This
preserves side effects and keeps the receiver alive between calls. As in
AngelScript 2.38.0, increment and decrement are rejected for virtual
properties. Object-valued compound properties are deferred until the value
object model can preserve the official copy/mutation rules.

The differential case covers compact and explicit declarations, interface
dispatch, implicit `this`, and a side-effecting compound receiver against
AngelScript 2.38.0. Global and indexed property accessors remain outside this
class-focused stage.
