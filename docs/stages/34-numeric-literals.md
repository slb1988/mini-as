# Stage 34: Official numeric bases and suffixes

The scanner recognizes AngelScript's `0b`, `0o`, `0d`, and `0x` based integer
forms, decimal exponents, leading or trailing decimal points, and the `f`
suffix. Unsuffixed real literals are `double`; suffixed real literals are
`float`.

Decimal integers select `int` through `INT32_MAX`, then `int64` through
`INT64_MAX`, and finally `uint64`. Based literals select `uint` when they fit in
32 bits and `uint64` otherwise. Values beyond 64 bits and base prefixes without
digits are compile diagnostics rather than truncated constants.

`DecodeNumericLiteral` is shared by type checking, constant evaluation,
bytecode constants, and the milestone-zero interpreter. This keeps a literal's
type and exact bits identical through every execution path. Arithmetic still
uses the explicit promotion and conversion rules introduced by stages 32 and
33.
