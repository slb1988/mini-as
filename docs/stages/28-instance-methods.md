# Stage 28: Instance methods and implicit this

Class method bodies are now type checked and compiled. A method is an ordinary
bytecode function with a stable `FunctionId`, an owning `TypeId`, and a hidden
local slot zero containing the receiver. Explicit parameters follow that slot.

Inside a method, unresolved identifiers fall back to fields on the current
class, and unqualified method calls load the hidden receiver automatically.
Explicit `object.method(args)` calls evaluate the receiver before arguments and
use a `ScriptMethod` callable descriptor. The VM includes the receiver when it
constructs the callee's local frame, reusing the existing call/return machinery.
