# Stage 29: Overloaded constructors

Class bodies accept constructor declarations whose name matches the class and
whose return type is implicit. Constructor overloads are selected by arity and
the existing conversion-cost rule; duplicate parameter lists and missing
matches are compile errors.

Object construction emits `NEW_OBJECT`, preserves one handle as the expression
result, and invokes the selected constructor as a `ScriptMethod` with the new
object as hidden `this`. The constructor's void result is discarded, leaving the
initialized handle on the operand stack. Declaring any constructor disables the
implicit zero-argument constructor unless an explicit zero-argument overload is
present.
