# Stage 19: Module global variables

Top-level variable declarations now allocate stable `GlobalId` slots in the
module image. Functions resolve global reads and writes through `LOAD_GLOBAL`
and `STORE_GLOBAL`, so bytecode does not retain addresses into a movable value
container.

Each successful build creates a fresh mutable `ModuleState` next to the
immutable bytecode image. Initializers execute in declaration order after the
image has been linked. Contexts prepared from the same image share that state;
contexts holding an older image keep its earlier state across a rebuild.

Primitive and string globals receive deterministic default values before
explicit initializers run. A compile error or runtime exception during global
initialization leaves the last successful image and its state untouched. Const
globals use the same assignment protection as const locals.
