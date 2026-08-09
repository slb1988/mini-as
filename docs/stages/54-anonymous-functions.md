# Stage 54: anonymous functions and captured locals

Scripts can now create funcdef handles with AngelScript's anonymous function
syntax:

```angelscript
funcdef int Binary(int, int);
Binary@ operation = function(left, right) { return left + right; };
```

The target funcdef supplies omitted parameter types and modes. Explicitly typed
parameters can disambiguate otherwise matching funcdefs. The lambda body is
type-checked as a nested function with the funcdef's return and reference
contract; missing matches and ambiguous inference are compile errors.

Each anonymous function is lowered to a hidden bytecode function with a stable
`FunctionId`. `MakeClosure` creates the ordinary typed `FunctionHandle`, so
calls, assignments, parameters, return values, exception unwinding, and module
image lifetime reuse the Stage 52 machinery. Hidden names derive from source
section and location and therefore remain stable across unchanged rebuilds.

AngelScript 2.38.0 supports anonymous functions but explicitly does not allow
them to access enclosing locals. The roadmap asks for captured locals, so mini
adds that behavior as a documented extension. Referenced outer locals are
promoted lazily to shared mutable cells. The creating scope, multiple closures,
and nested closures share the same cell: updates remain visible in every owner,
and the cells keep their values alive after the original stack frame returns.
Module globals continue to use global slots and are not copied into a closure.

Captured object handles participate in reference enumeration through the
closure environment. If an object stores a closure that captures the same
object, the existing cycle collector sees and clears that edge. Capturing
implicit `this` is rejected for now; scripts can capture an explicit local
object handle instead. This keeps the closure representation independent of a
hidden method frame while preserving the common callback use case.

Tests cover tokenizer and AST shape, inferred and explicit parameter types,
ambiguous inference diagnostics, emitted closure bytecode, mutable captures,
post-scope lifetime, exception body/call locations, and captured-object GC
cycles. The differential case is intentionally non-capturing—the common subset
implemented by AngelScript 2.38.0—and returns `42` in both runtimes.
