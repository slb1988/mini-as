# Stage 84: mixin classes

## Goal

Mixin classes provide reusable partial class definitions without introducing a
runtime type:

```angelscript
mixin class Reusable {
    int value;
    int read() { return value; }
}

class Concrete : Reusable {}
```

`Reusable` cannot be instantiated, used as a field/parameter/return type, or
published through reflection. Mixins cannot be shared or external, cannot have
constructors, destructors, or child funcdefs, and may implement interfaces but
cannot inherit classes or other mixins.

## AST expansion and precedence

The parser records a dedicated `MixinDecl`, then expands each included mixin
into the concrete class with arena-owned deep copies. Copied members preserve
their original source locations but are compiled with the including class as
implicit `this`. This lets a mixin method refer to fields or methods supplied
only by the target class.

Explicit class members take precedence over matching mixin members. Mixin
methods are otherwise treated as methods declared by the derived class and
therefore override inherited base methods. Mixin fields that conflict with an
explicit or inherited field are omitted; their initializer is omitted too.
Interfaces listed by a mixin are transferred to the including class.

The expansion is performed only after the complete module has parsed, so a
class may include a mixin declared in a later script section or namespace.

## Bytecode and verification

Bytecode format version 6 persists the mixin token, declaration kind, expanded
member marker, and retained syntax tree. No mixin-specific runtime opcode is
needed: after expansion, the ordinary class layout, type checker, compiler,
virtual dispatch, reflection, and VM paths apply.

Tests cover tokenization, AST expansion, interfaces, target-class lookup,
explicit/base/mixin precedence, inherited-field suppression, non-instantiability,
invalid declarations, original-source exception locations, bytecode save/load,
and differential execution against AngelScript 2.38.0.
