# mini_angelscript v0.5 embedding compatibility matrix

## Baseline and scope

This matrix records the embedding-facing subset aligned with AngelScript 2.38.0
at the end of v0.5. It includes the complete v0.4 script subset and adds host
registration, stable reflection, dynamic module functions, versioned bytecode,
debug inspection, and an official-style facade.

mini_angelscript remains a C++17 teaching implementation built around typed
bytecode, `std::variant<Value>`, and the portable `GenericCall` interface.
Compatibility means matching observable behavior for the documented subset. It
does not imply AngelScript SDK header, source, binary, native calling-convention,
or serialized-bytecode compatibility.

## Host registration

| Capability | v0.5 status | Notes |
| --- | --- | --- |
| Global properties | Supported subset | Primitive and string storage is registered by declaration, receives a stable `GlobalId`, shares state across contexts, and enforces `const` writes. Object and function-handle global storage is deferred. |
| Reference types and factories | Supported subset | Registered reference types use `ObjectHandle`, overloaded factories, and `GenericCall`. Reference counting is intrinsic to the mini object model; arbitrary SDK behaviour registration is not exposed. |
| Object methods | Supported subset | Registered methods receive the object through `GenericCall`, support overload resolution and const receivers, and implement reference-parameter copy-in/copy-out. Native ABI calling conventions are intentionally excluded. |
| Object properties | Supported subset | Getter/setter callbacks expose registered properties and preserve read-only declarations. The supported value and handle forms follow the current `Value` model. |
| Value types | Supported subset | Host values use `HostValueStorage`, support default/copy construction and const methods, and participate in typed calls. Mutable receiver write-back and the complete SDK behaviour set remain unsupported. |
| Enums, typedefs, and funcdefs | Supported | Registered declarations are visible to parsing, type checking, overload resolution, and reflection with stable type identity. |
| Registration namespaces | Supported subset | Engine and module default namespaces resolve relative declarations and fully qualified lookup. Namespace aliases and the full SDK namespace API are not claimed. |
| Access masks | Supported subset | Registrations retain their mask; each module selects the registrations visible to a build. Existing immutable module images keep the environment with which they were built. |
| Configuration groups | Supported subset | Registrations can be grouped and a group can be removed when no module image depends on it. Removal deactivates entries without invalidating stable metadata addresses. |
| Calling conventions | Intentional divergence | All host calls use `GenericCall`; native `cdecl`, `thiscall`, platform ABI bridges, and auxiliary object calling conventions are out of scope. |

## Reflection

| Capability | v0.5 status | Notes |
| --- | --- | --- |
| Type metadata | Supported subset | Stable `TypeMetadata` describes script and host objects, enums, typedefs, and funcdefs, including fields, methods, inheritance, interfaces, underlying types, and enum values where applicable. |
| Function metadata | Supported subset | Stable `FunctionMetadata` covers registered globals and built script functions/methods by `FunctionId`, declaration, and module name. |
| Module-global metadata | Supported subset | `GlobalMetadata` provides stable IDs and indexed/name/declaration lookup for script globals. Registered host properties remain engine registrations and are not counted as module globals. |
| Pointer stability | Supported | Public metadata is stored at stable addresses, and IDs remain the bytecode/runtime identity even as registrations and module images grow. |
| Official reflection interfaces | Not source compatible | Metadata is deliberately compact and does not reproduce the complete `asITypeInfo` or `asIScriptFunction` method sets, user-data slots, reference ownership, or SDK interface vtables. |

## Modules and bytecode

| Capability | v0.5 status | Notes |
| --- | --- | --- |
| Transactional module rebuild | Supported | A failed build preserves the last successful immutable `ModuleImage`; a prepared context owns its image snapshot and can finish after a later rebuild. |
| Dynamic function compilation | Supported subset | Functions can be compiled as attached or detached definitions with section names, line offsets, namespaces, default arguments, and the current module environment. |
| Dynamic function removal | Supported | Removed functions disappear from public lookup while retired bytecode stays alive for old descriptors and prepared contexts. |
| Versioned bytecode save/load | Supported, private format | The little-endian `MASB` v2 format validates version, bounds, payload, and checksum; it saves definitions, typed bytecode, debug metadata, environment, and removal state, remaps stable IDs, rebinds host registrations, and initializes globals transactionally on load. |
| Official bytecode compatibility | Not supported | mini bytecode is neither readable by nor produced by AngelScript 2.38.0. Differential coverage compares source behavior, not internal instructions or serialized bytes. |
| Live-state serialization | Deferred | Module-global values, object graphs, suspended contexts, and external resource state are v0.6 work. |

The mini removal model deliberately preserves more old-image state than the
official save/load path can represent after a referenced function has been
removed. The differential save/load case therefore compares the common state
before removal; the stronger mini snapshot behavior has dedicated native tests.

## Debugging and runtime inspection

| Capability | v0.5 status | Notes |
| --- | --- | --- |
| Line callback and execution control | Supported subset | Contexts expose source locations and can suspend, resume, or abort execution. |
| Call-stack inspection | Supported | Frames use `0` for the current frame and expose function identity and instruction source location during callbacks and after exceptions. |
| Local-variable inspection | Supported subset | Name, type, const/parameter flags, lexical scope, and copied values are available; captured cells are unwrapped for inspection. Values are snapshots, not writable raw stack addresses. |
| Exception snapshots | Supported | Located runtime failures preserve the stack and visible locals for post-failure inspection. |
| Garbage collection | Retained v0.4 model | Explicit collection handles tracked script-object cycles, but collection is still full-cycle rather than incremental and does not yet expose official-style statistics or circular-reference callbacks. |
| Context pooling and coroutines | Deferred | Reuse policies, pooling callbacks, and cooperative coroutine scheduling are v0.6 work. |

## Official-style compatibility facade

`include/mini_as/compat.hpp` provides an opt-in `mini_as::compat` facade. It
uses AngelScript 2.38.0-style integer result codes, execution states, module
policies, and compile flags while forwarding to the existing RAII API.

| Facade area | v0.5 status | Notes |
| --- | --- | --- |
| Engine/module/context workflow | Supported subset | Creation, module selection, section build, lookup, preparation, arguments, execution, return values, exceptions, dynamic compilation/removal, bytecode, and debug inspection are covered. |
| Registration facade | Partial | Global functions/properties, enums, typedefs, and funcdefs are wrapped directly. Object registration remains available through `Native()` rather than pretending the full SDK surface exists. |
| Host controls | Supported subset | Default namespaces, access masks, and configuration groups use official-style names and results. |
| Ownership model | Intentional divergence | The facade retains C++ RAII (`std::unique_ptr`) and engine-owned modules; it does not expose SDK `AddRef`/`Release`, interface vtables, or header/source compatibility. |
| Error mapping | Supported subset | Common results map to official numeric constants. Operations without enough public detail can still collapse to a general error instead of reproducing every SDK failure distinction. |

The original `mini_as::ScriptEngine`, `ScriptModule`, and `ScriptContext` API
remains available and retains its existing semantics.

## Differential and build coverage

With `MINI_AS_BUILD_COMPAT_TESTS=ON`, the suite builds the official engine from
the exact 2.38.0 tag and compares normalized script output, execution state,
return values, and relevant diagnostics. v0.5 adds cases for global properties,
reference factories, registered object methods/properties/value types and named
types, debug introspection, the compatibility facade, and host registration
controls. It never compares private bytecode or ABI details.

At this milestone the configured suite contains 56 CTest entries, including 54
differential cases. Local acceptance prioritizes MSVC `/W4` and the validated
GCC build; CI remains responsible for GCC, Clang, MSVC, and Linux ASan/UBSan.
The Windows Clang sanitizer path is not used locally because its crash dialog
can block unattended runs.

## Retained language surface and intentional deviations

All documented v0.2-v0.4 language behavior remains supported: globals and
structured control flow, numeric families and operators, functions and
reference modes, classes/interfaces/inheritance, exceptions, function objects,
weak references, and generated/deleted copy/default operations.

String remains a built-in `Value` for readability even though official
AngelScript normally supplies string through an add-on. Native ABI bridges,
SDK header compatibility, JIT integration, and production-grade optimizer or
hard-real-time GC behavior remain explicitly outside the project roadmap.

## Next alignment boundary

v0.6 begins with registered template types and the array add-on, followed by
initialization lists, indexing, and foreach. It then covers dictionary/any/ref,
imports and shared/external entities, mixins, incremental GC and statistics,
context pooling and coroutines, live-state serialization, variadic arguments,
and registered template functions.
