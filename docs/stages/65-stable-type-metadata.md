# Stage 65: stable type reflection metadata

The engine now exposes one stable reflection record for every published object,
enum, typedef, and funcdef:

```cpp
const mini_as::TypeMetadata* type =
    engine->GetTypeMetadataByName("HostColor");

type = engine->GetTypeMetadataById(type->id);
type = engine->GetTypeMetadataByIndex(0);
```

`TypeMetadata` carries the stable `TypeId`, name, category, host/script origin,
object fields and methods, inheritance information, enum values, typedef
underlying type, or funcdef signature as appropriate. The returned record lives
in engine-owned `std::deque` storage. Adding types or publishing a rebuilt type
does not change the record's address; a rebuild updates its contents in place.

Host registrations publish immediately. Later object method/property and enum
value registrations refresh the same record. Script declarations are different:
the compiler assigns their durable ids first, but the engine publishes their
metadata only after parsing, type checking, bytecode generation, linking, and
module-global initialization all succeed. A failed rebuild therefore leaves both
the last successful `ModuleImage` and its reflected type view intact.

AngelScript 2.38.0 exposes `asITypeInfo` through `GetTypeInfoById`,
`GetTypeInfoByName`, and category-specific index APIs. mini_as keeps its existing
RAII API and presents a compact `TypeMetadata` value instead of cloning that
interface. The later `mini_as::compat` facade can map official-style queries onto
this stable catalog.

The local reference checkout at `D:\Github\angelscript2` is newer than the
2.38.0 tag, so comparisons for this stage use the repository's `v2.38.0` tag
content as the authority. Tests cover all four categories, lookup by name/id/index,
pointer stability across host updates and successful rebuilds, invalid lookups,
and non-publication after a global-initializer failure.
