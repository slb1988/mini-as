# Stage 92: registered template functions

## Goal

Add AngelScript 2.38.0-style template syntax to registered generic functions
and object methods while keeping mini_as's callback-only host boundary:

```angelscript
T Identity<class T>(T value)
T choose<T, U>(T first, U second)
T Thing::echo<T>(T value) const
```

Scripts instantiate these definitions explicitly, for example
`Identity<int>(42)` or `value.echo<string>("hello")`. The implementation does
not add native calling conventions or script-defined template functions.

## Compiler model

A registered template definition stores its ordered parameter names in
`FunctionSignature::templateParameters`. The parser recognizes `<...>` only
when the qualified function or method name is known to be a registered
template, so ordinary comparison expressions remain unambiguous. Each explicit
use is collected before type checking and materialized as a closed signature
with stable `FunctionId` and concrete `templateArguments`.

Substitution currently supports a template parameter used directly as a value
or handle type (`T` and `T@`). Nested substitution such as `array<T>`, argument
type inference, default template arguments, and template specializations are
deliberately deferred. Omitting the explicit type list therefore does not
select a template definition. A closed signature that collides with an
ordinary registered overload is rejected rather than silently replacing it.

Closed instances participate in the normal overload, function-handle, object
method, reflection, and bytecode pipelines. Open definitions are parser inputs
and metadata only; they are excluded from ordinary callable lookup. Multiple
template definitions with the same name and template arity instantiate and
compete through the normal overload-resolution rules.

## Runtime bridge

Template instances reuse the definition's `GenericCall` callback and host
registration controls. `GenericCall::GetTemplateArgCount()` and
`GetTemplateArgType(index)` expose the concrete type list, while existing
argument and return accessors continue to carry the instantiated values.

Instances are cached engine-wide so modules use the same stable ids. Bytecode
format version 8 persists template definitions, closed arguments, and explicit
AST uses. Loading an archive recreates the required host instances before
symbol remapping, then binds callbacks by the concrete declaration.

## Verification

Tests cover global functions, object methods, namespaces, multiple template
parameters, primitive and string instantiations, function handles, reflection,
bytecode round trips, declaration and use diagnostics, overload collisions,
invalid primitive handles, and source-located callback exceptions. The
differential corpus executes an explicit `Identity<int>` call against the local
AngelScript 2.38.0 source build.
