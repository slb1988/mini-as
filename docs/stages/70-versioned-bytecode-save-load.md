# Stage 70: versioned bytecode save and load

Modules can now persist and restore their compiled image through standard C++
streams:

```cpp
std::stringstream storage(std::ios::in | std::ios::out | std::ios::binary);
module->SaveBytecode(storage);

storage.seekg(0);
otherModule->LoadBytecode(storage);
```

The binary format begins with the `MASB` magic and an explicit format version.
All integers use fixed little-endian encoding. Length-prefixed strings and
collections have defensive upper bounds, the payload has an exact declared
length, enum and opcode values are validated, and an FNV-1a checksum rejects
corruption before any module state changes. Version 1 is intentionally an
internal mini_as format; it does not claim compatibility with AngelScript SDK
bytecode.

The archive stores typed functions, instructions, constants, exception handlers,
call descriptors, virtual dispatch, global bindings and initializer bytecode,
destructor links, funcdefs, the complete compilation environment, removed
function ids, and arena-owned typed definition trees. Restoring the definition
trees lets later dynamic compilations continue materializing old default
argument expressions in their defining scopes.

Stable ids are engine-local, so loading never trusts serialized numeric ids as
live identities. Script types, functions, and globals receive ids for the target
module; every instruction, callable, dispatch entry, destructor, constant
function handle, reference, and removal marker is remapped. Host functions,
properties, object/value types, enums, typedefs, and funcdefs must match current
registrations and are rebound to their current ids and callbacks.

Loading is transactional. The complete archive is parsed, checked, remapped,
linked, and its module-global initializer is executed in a candidate state first.
Only then are reflection metadata and a new immutable `ModuleImage` published.
Bad magic, unsupported versions, truncation, length errors, checksum failures,
missing host registrations, and failed global initialization preserve the old
image.

Like AngelScript's bytecode API, this feature stores compiled program state, not
live runtime object graphs. Module globals are recreated and initialized when
loaded. Serialization of live globals, objects, and suspended contexts remains
in the later v0.6 serialization stages.

Tests cover cross-engine id remapping, classes, enums, globals, dynamic and
retired functions, reflection, post-load incremental compilation with a saved
default expression, host rebinding, configuration mismatch, version rejection,
truncation, corruption, and atomic rollback. The differential runner performs a
valid save/load round trip with the exact AngelScript `v2.38.0` source from
`D:\Github\angelscript2`.
