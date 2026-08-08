# Stage 20: For loops

The parser models a `for` statement as four explicit children: initializer,
condition, increment, and body. Missing clauses use `EmptyStmt`, which avoids
guessing child positions in later compiler passes.

The initializer and loop body share a lexical scope. The condition must be
boolean when present; an omitted condition is emitted as `true`. Bytecode jumps
from the body through the increment expression and back to the condition, while
discarding the increment expression's value.

The initializer accepts empty, expression, single declaration, and
comma-separated declaration forms.
