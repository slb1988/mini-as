# Stage 55: child funcdefs

Funcdefs can now be declared as members of script classes. A child funcdef has
the fully qualified type name `Parent::Name`, while code inside the parent may
use the short name. As in AngelScript 2.38.0, the type is also visible by its
short name in derived-class scope and by its qualified name elsewhere.

```angelscript
class Dispatcher {
    funcdef int Callback(int value);
    Callback@ callback;
}

class DerivedDispatcher : Dispatcher {
    Callback@ inheritedCallback;
}

Dispatcher::Callback@ callback = @twice;
```

The parser's declaration pre-scan records class-owned funcdef names and base
relationships before parsing bodies. This makes type lookup independent of
source order, including a qualified use that appears before the parent class.
Child declarations remain dedicated `FuncdefDecl` nodes inside the class AST;
they are not mistaken for fields or body-less methods.

`FuncdefSignature` now records `parentType` in addition to its canonical name,
signature, and stable `TypeId`. Two classes may declare identically named and
shaped child funcdefs, but the resulting handle types remain distinct because
their qualified names and IDs are distinct. No new call opcode is necessary:
child handles use the existing function-address, field/global/local storage,
`CallHandle`, and null-handle exception paths.

The official grammar permits child funcdefs in classes but not interfaces, so
interface declarations are diagnosed. Duplicate declarations in one parent,
cross-parent handle assignment, and default arguments retain the normal
funcdef diagnostics.

Tests cover AST qualification, forward lookup, inherited lookup, parent
metadata, distinct type identity, runtime invocation through a class field,
null-handle source locations, and an unchanged differential case executed by
both mini_as and AngelScript 2.38.0. Public child-funcdef reflection accessors
remain part of the later reflection roadmap.
