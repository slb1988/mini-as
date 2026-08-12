# Stage 40: Default arguments

Function, method, and constructor parameters may declare trailing default
expressions. Calls remain eligible when they provide every required parameter;
after overload selection, omitted arguments are compiled at the call site in
the namespace where the callable was declared.

Default expressions use the normal expression type checker and bytecode
compiler, so they may read module globals and produce ordinary located runtime
exceptions. The callable descriptor still records the full parameter count,
keeping stack layout identical to an explicit call.

`FunctionSignature::defaultArgumentCount` is durable metadata, while the AST
owns the expressions only during module compilation. No AST pointers escape
into `ModuleImage`, preserving rebuild snapshot safety.
