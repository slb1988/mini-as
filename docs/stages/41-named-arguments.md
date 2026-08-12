# Stage 41: Named arguments

Call arguments may use AngelScript's `parameter: expression` syntax. Once a
named argument appears, later positional arguments are rejected. Named
arguments can be reordered and may skip parameters that have defaults.

Parameter names are retained in `FunctionSignature`. Type checking maps each
call argument to a unique parameter before calculating conversion costs, so an
unknown name, a duplicate name, or an omitted required parameter makes that
overload non-viable. Bytecode repeats the same mapping and emits values in
parameter-slot order before invoking the existing callable descriptor.

The implementation applies uniformly to functions, methods, constructors, and
registered host functions whose declarations include parameter names.
