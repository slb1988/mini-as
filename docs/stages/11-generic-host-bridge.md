# 11 - Generic host calling bridge

Registration declarations are parsed into the same `DataType` and function
signature model used for scripts. Compilation therefore resolves host calls
and inserts conversions before runtime. `CALL_HOST` only moves already checked
values into `GenericCall`, invokes a type-erased callback, and validates the
declared return type.

This deliberately implements AngelScript's portable generic convention rather
than a platform ABI bridge. No VM value is cast into CPU argument registers and
no assembly is required; the semantics of registration, overload matching,
exception propagation, and host isolation remain visible.

Tests cover conversion at a host call, explicit host exceptions, invalid
declarations, reference-style declaration text, and duplicate registration.
