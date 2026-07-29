# 03 - Parser and AST

The parser is recursive descent: one function owns each precedence level, so
the call graph is the grammar. `a + b * c` becomes a `+` node whose right child
is `*`; no later precedence repair is needed.

All syntax uses one `AstNode` structure. An arena owns nodes while
`firstChild/nextSibling` expresses shape without a C++ inheritance hierarchy.
This follows AngelScript's compact tree design and makes traversal identical
for declarations, statements, and expressions.

The parser accepts function and class shapes before later stages can execute
them. This deliberate two-step development keeps syntax, type rules, and
runtime behavior independently testable. Error recovery advances to statement
boundaries so a malformed source file can report more than one problem.
