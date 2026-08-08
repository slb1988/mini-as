# Stage 24: Continue statements

`continue` is valid only inside loops. The type checker tracks loop depth
separately from breakable depth, so a switch alone permits `break` but not
`continue`.

The compiler searches outward for the nearest control-flow context marked as a
loop, allowing `continue` inside a switch nested in a loop. Pending jumps are
patched to the condition for `while`, the condition for `do`/`while`, and the
increment segment for `for`, matching AngelScript's loop semantics.
