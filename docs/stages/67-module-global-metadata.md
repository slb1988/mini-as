# Stage 67: module-global reflection metadata

Script modules now expose stable reflection records for their own global
variables:

```cpp
const mini_as::GlobalMetadata* global =
    module->GetGlobalMetadataByDecl("const int answer");

global = module->GetGlobalMetadataByName("answer");
global = module->GetGlobalMetadataById(global->id);
global = module->GetGlobalMetadataByIndex(0);
```

`GlobalMetadata` carries the durable `GlobalId`, owning module name, and
`GlobalSignature`. `GlobalSignature::Declaration()` provides the canonical
`[const] type name` spelling used by declaration lookup. Fully qualified names
are retained for globals declared in namespaces.

The metadata records live in an engine-owned `std::deque`. A successful rebuild
refreshes a matching id in place, while adding globals cannot invalidate cached
record pointers. Module lookup first checks the current immutable `ModuleImage`,
so removed or foreign globals are not accidentally exposed through a module.

Publication happens only after parsing, type checking, bytecode generation,
linking, and module-global initialization all succeed. Failed rebuilds preserve
the previous image, shared global state, declarations, and metadata records as
one atomic view.

Registered host global properties are intentionally excluded. AngelScript
2.38.0 separates engine-level registered properties from
`asIScriptModule::GetGlobalVar*`, which enumerates only script globals. mini_as
follows that boundary: `GetGlobalMetadataCount()` and index lookup skip host
bindings even though those bindings remain available to compiled scripts.

The differential runner checks the same module-global count and declaration
lookups with the exact `v2.38.0` source from `D:\Github\angelscript2`. Direct
storage addresses and variable removal remain later compatibility and dynamic
module-management concerns rather than metadata responsibilities.
