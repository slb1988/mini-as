# Stage 57: generated copy constructors

Script classes now receive an implicit copy constructor unless they declare any
single-argument constructor. This follows AngelScript 2.38.0's generation rule:
the suppressing constructor does not need to accept the class's own type.

```angelscript
Box@ original = Box();
original.value = 42;
Box@ copied = Box(original);
```

`ClassSignature::generatedCopyConstructor` records the decision without
inventing an AST function body or unstable callable target. When overload
resolution selects the generated operation, the compiler emits `NewObject`
followed by `CopyObject`. The VM validates both objects against the stable target
`TypeId`, then copies the destination class's flattened field range. A derived
source can therefore be sliced into a base destination using the same inherited
field prefix used by normal field access.

The copy is member-wise. Integer, floating-point, boolean, enum, and string
values are copied independently. Object and function handles remain handles to
the same targets, and weak references retain the same non-owning lifetime token.
Field initializers and default/base constructors are not executed for the new
copy because the source fields directly initialize the destination. A null
source raises a VM exception at the constructor call location.

The differential case checks inherited value fields and shared object handles
against the official 2.38.0 engine. Unit coverage verifies metadata generation,
the `CopyObject` operand, successful runtime behavior, suppression by an
unrelated one-argument constructor, and null-source exception locations.

mini_as still models script instances as reference-counted handles internally.
Accordingly, this stage exposes generated copies through the existing
`Class(source)` expression and `Class@` storage spelling; full value-object
storage remains outside the teaching runtime's current object model.
