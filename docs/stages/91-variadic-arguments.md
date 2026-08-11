# Stage 91: variadic arguments

## Goal

Align registered generic functions with AngelScript 2.38.0's variadic calling
model. A declaration's final parameter is the repeated prototype, for example:

```angelscript
int sum(int seed, int ...)
int formatCount(const string &in format, const ? &in ...)
void scan(const string &in input, ? &out ...)
```

The variadic segment must receive at least one argument, matching 2.38.0. The
feature is limited to registered `GenericCall` callbacks: script functions
cannot declare `...`, and mini_as still has no native ABI calling convention.

## Compiler model

`FunctionSignature::variadic` marks the last parameter as a prototype rather
than a fixed slot. Argument ordering keeps the fixed prefix and appends every
positional variadic expression. Named arguments may target only the fixed
prefix. Type checking repeats the prototype's type and reference mode for the
tail and gives any viable non-variadic overload priority over a variadic one.

The wildcard `?` exists only in registered parameter declarations. It accepts
the official `const ? &in ...` and `? &out ...` forms, while `? &inout ...`, a
bare `?`, and non-trailing ellipses are rejected. Fixed-type variadics may use
value or reference parameters.

Each bytecode `CallableRef` records the call site's actual argument count. This
is essential because two calls to the same registered function can now pop
different stack shapes. Bytecode format version 7 persists the variadic flag;
loading still rebinds host callbacks by their stable declaration.

## Runtime bridge

`GenericCall::GetArgCount()` reports the complete fixed-plus-variadic count and
`GetArgType(index)` exposes the concrete mini_as `DataType`. Wildcard input
arguments keep their original `Value` representation. Wildcard output slots
are initialized with the destination lvalue's actual type, and the VM verifies
that the callback writes the same type before writeback.

Registered global functions, reference-type factories, object methods, and
host function handles share this path. Host exceptions and incompatible
wildcard output writes retain the script call site's source location.

## Verification

Tests cover declaration diagnostics, fixed and wildcard tails, minimum tail
cardinality, exact-overload preference, dynamic call descriptors, wildcard
output writeback and type failures, object factories and methods, bytecode
round trips, and source-located runtime errors. The differential corpus runs a
fixed-type variadic script against the local AngelScript 2.38.0 source build.
