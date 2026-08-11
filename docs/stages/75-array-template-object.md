# Stage 75: array template object

## Goal

The first standard add-on is now available as an ordinary registered template
type rather than a VM primitive:

```cpp
#include "mini_as/addons/array.hpp"

auto engine = mini_as::CreateScriptEngine();
mini_as::addons::RegisterScriptArray(*engine);
```

Registration creates `array<class T>`. Closed instances such as `array<int>`
are configured lazily by the template-instance callback introduced in Stage 74.

## Object and method surface

`ScriptArray` is a reference-counted `RefObject` holding typed `Value` elements.
Each closed instance registers portable `GenericCall` factories and methods:

- default and `uint length` construction;
- `length()` and `isEmpty()`;
- `resize(uint)`;
- `insertLast(T)` and `removeLast()`;
- explicit `get(uint)` and `set(uint, T)` helpers.

Primitive, enum, string, function, weak-reference, host-value, and object-handle
elements use the existing `Value` copy/lifetime semantics. A reference object
used as an element subtype must currently be spelled as a handle (`T@`), because
the mini object model does not yet construct arbitrary registered reference
objects by value.

Initialization-list factories and `[]` syntax are intentionally absent here;
they are the next two independent language commits.

## Lifetime and bytecode

Array instances participate in the existing cycle collector. They enumerate
object references held directly, through delegates, or through captured cells,
and clear their elements during cycle reclamation. A script object and an array
can therefore form a collectable cycle.

Template instance callbacks inherit the definition's access mask and
configuration group. Partial callback registration is deactivated if instance
configuration fails.

Bytecode loading now materializes archived closed host-template instances before
stable ID remapping and host callback rebinding. A module using `array<int>` can
therefore be saved, loaded into a newly configured engine, and executed without
first compiling a source module to prime that specialization.

## Diagnostics and verification

Out-of-range `get`/`set` calls and removal from an empty array become located VM
exceptions at the script call site. Unsupported element subtypes are rejected at
the template use location during candidate module construction.

Tests cover typed construction and defaults, resizing, insertion/removal,
element reads/writes, runtime error locations, invalid subtype diagnostics,
cycle collection, and cross-engine bytecode restore. The differential case uses
the official AngelScript 2.38.0 `scriptarray` add-on and compares the same
construction/resize/insert/remove/length script.
