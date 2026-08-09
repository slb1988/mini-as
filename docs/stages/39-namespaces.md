# Stage 39: Namespaces and scope resolution

Named and nested `namespace` blocks isolate module declarations. Functions,
globals, classes, interfaces, enums, and typedefs receive canonical qualified
names such as `Root::Left::read`, while the AST retains its `NamespaceDecl`
hierarchy for tooling and diagnostics.

Unqualified references search the current namespace, each parent namespace,
and finally the global namespace. The `::` operator selects an explicit
qualified function, global, enum value, or type. Type checking and bytecode use
the same candidate order, so overload selection cannot silently change between
the two compiler stages.

Qualified names are also the keys used to allocate stable `TypeId`,
`FunctionId`, and `GlobalId` values. Two namespaces may therefore declare the
same simple name without colliding, while duplicate declarations within one
namespace remain diagnostics.
