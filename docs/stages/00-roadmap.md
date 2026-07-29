# 00 - Roadmap

The repository follows source text through tokens, AST, type checking,
bytecode, modules, and an explicitly managed VM context. Later stages add
generic host calls, reference objects, script classes, and cycle collection.

Every stage is kept as a buildable Git commit. The source uses the standard
library where the production AngelScript engine uses custom containers so the
engine algorithms remain visible without portability scaffolding dominating
the lesson.
