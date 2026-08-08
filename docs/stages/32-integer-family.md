# Stage 32: Signed and unsigned integer family

The language accepts AngelScript's `int8`, `int16`, `int`/`int32`, `int64`,
`uint8`, `uint16`, `uint`/`uint32`, and `uint64` names. `int` remains the
existing 32-bit `Value` alternative so the original embedding API stays source
compatible. Other widths use a tagged integer payload containing their exact
type and normalized bits.

Integer conversions are explicit `TO_INTEGER` bytecode operations. Narrowing
uses two's-complement truncation, so stored values and increment/compound
assignment wrap at the declared width. Arithmetic promotes 8- and 16-bit
operands to `int`, then selects the wider type; an unsigned operand wins when
it has at least the signed operand's width. Integer-to-`float` remains an
allowed widening conversion.

The VM performs add, subtract, multiply, negate, divide, remainder, and ordered
comparison without routing integer values through floating point. This keeps
64-bit values exact and gives division-by-zero the existing located exception
behavior. Numeric-base prefixes and literal suffixes are deliberately left for
the separate literals feature.
