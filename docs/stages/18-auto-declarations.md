# Stage 18: Auto declarations

`auto` declarations infer their type from the initializer after the expression
has been type checked. In a comma-separated statement, the first initializer
determines the shared declaration type, matching AngelScript's declaration rule.
Each declared name becomes visible before the next initializer is checked.

An initializer is mandatory because there is no declared type to fall back to.
The inferred type is written back to the ordinary `VarDecl` node, so bytecode
generation and numeric conversions use the same path as explicit declarations.

`const auto` composes inference with the read-only symbol protection from stage
17. Object-producing expressions retain their inferred handle type.
