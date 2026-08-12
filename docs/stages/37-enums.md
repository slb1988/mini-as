# Stage 37: Enums

Script enums are named 32-bit integer types. Enumerators start at zero, advance
by one, and may use integer constant expressions that reference values declared
earlier. Their names are visible as module constants and cannot be assigned.

The parser discovers enum type names before parsing declarations, then records
`EnumDecl` and `EnumValue` nodes. The type checker evaluates and range-checks
each value, preserves the enum name in `DataType`, and exposes stable enum
metadata. Bytecode embeds enum values as typed constants while reusing the
existing integer arithmetic and comparison instructions.

Enums convert to the built-in numeric types, matching their role as integer
constants, but arbitrary integers do not implicitly convert back to an enum.
This keeps accidental assignments diagnosable without adding a separate VM
execution path.
