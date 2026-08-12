# Stage 33: Double precision values

`double` is now a first-class `DataType` and `Value` alternative. Stage 34
completes literal typing: unsuffixed real literals are doubles and the `f`
suffix selects float. Integer constants can initialize doubles exactly.

The compiler emits `TO_DOUBLE` when widening integers or floats, `TO_FLOAT`
when narrowing a double, and dedicated `ADD_D`, `SUB_D`, `MUL_D`, `DIV_D`, and
`NEG_D` instructions. Mixed numeric expressions select double before float,
then the integer promotion rules. The VM keeps comparisons and arithmetic in
double precision and reports division by zero at the originating instruction.

The existing float API remains unchanged. `ScriptContext` adds
`SetArgDouble`/`GetReturnDouble`, while `GenericCall` adds matching double
accessors so the portable host bridge can exchange the new value type without
an ABI-specific calling convention.
