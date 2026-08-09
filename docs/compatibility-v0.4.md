# mini_angelscript v0.4 compatibility matrix

## Baseline and scope

This matrix records the source-level subset aligned with AngelScript 2.38.0 at
the end of v0.4. It includes every v0.2 and v0.3 capability and adds function
objects, weak references, and the copy/default-operation lifecycle rules. The
project remains a C++17 teaching implementation with typed bytecode,
`std::variant<Value>`, portable `GenericCall`, and the existing `mini_as` API.

Compatibility means that the documented script subset has matching observable
behavior. It does not imply SDK headers, native ABI calling conventions,
serialized bytecode, internal VM layout, or binary compatibility.

## Function types and callable values

| Capability | v0.4 status | Notes |
| --- | --- | --- |
| `funcdef` declarations | Supported | Global and namespace declarations preserve return/reference qualifiers, parameter types, and `in`/`out`/`inout` modes with stable `TypeId` metadata. |
| Function handles | Supported subset | Handles to matching global script and registered host functions can be stored, passed, returned, reassigned with explicit `@`, compared with `is`, and invoked. Public host reflection is deferred. |
| Delegates | Supported subset | `Funcdef(object.method)` binds script instance methods, holds a strong receiver reference, and dispatches overrides through stable virtual slots. Registered host object methods arrive in v0.5. |
| Anonymous functions | Supported plus extension | Non-capturing anonymous functions match the official 2.38.0 subset. Mutable lexical captures and nested capture cells are a mini_as extension; implicit `this` capture remains deferred. |
| Child funcdefs | Supported | Class-owned types use `Parent::Name`, short lookup inside the parent and derived classes, stable identity, and ordinary handle-call behavior. Interface child funcdefs are rejected. |
| Null callable behavior | Supported | Null function handles and null delegate receivers produce located VM exceptions; closure/delegate call stacks retain body and call-site locations. |

## Weak references and object graphs

| Capability | v0.4 status | Notes |
| --- | --- | --- |
| `weakref<T>` | Supported subset | The official add-on spelling, construction, `get()`, implicit locking, copying, equality, and explicit handle assignment work for script classes. |
| `const_weakref<T>` | Partial | Lifetime and type identity are preserved, but the current object-handle model cannot yet enforce read-only access through a locked const weak reference. |
| Lifetime race safety | Supported | Weak locking and the final strong `Release` synchronize on one lifetime token, preventing resurrection after the strong count reaches zero. |
| GC integration | Supported | Weak edges do not become collector roots; delegates and closure captures enumerate their strong object edges so callback cycles remain collectable. |
| Registered host weakrefs | Deferred | Weak-reference behaviours for registered host reference types depend on the v0.5 host-registration surface. |

## Construction, copying, and assignment

| Capability | v0.4 status | Notes |
| --- | --- | --- |
| Generated copy constructors | Supported subset | A class receives a generated copy unless any explicit one-argument constructor exists. Inherited/value fields are copied and handle fields retain shared identity. |
| Copy initialization order | Supported | Generated copies do not rerun field initializers, default constructors, or base default constructors. A null source reports the constructor call location. |
| Deleted default constructor | Supported | `Class() delete;` disables only implicit zero-argument construction; other explicit constructors remain available. Implicit derived construction checks deleted base defaults. |
| Deleted copy constructor | Supported | `Class(const Class &in) delete;` suppresses the generated copy path and makes `Class(source)` unavailable unless another viable explicit constructor exists. |
| Deleted copy assignment | Supported subset | `Class &opAssign(const Class &in) delete;` rejects value-style assignment while explicit `@left = right` handle rebinding remains legal. Generated member-wise `opAssign` is not otherwise modeled. |
| Script value-object storage | Partial | Script instances are still represented internally by reference-counted handles. Generated construction copies object state, but mini_as does not claim the official engine's complete value-object storage/destruction model. |

## Retained v0.3 language surface

| Area | v0.4 status | Notes |
| --- | --- | --- |
| Numeric and expression model | Retained | Signed/unsigned integer families, `float`/`double`, official numeric bases and suffix subset, bitwise/shifts, exponentiation, enums, casts, and operator overloads remain covered. |
| Functions and control flow | Retained | Defaults, named arguments, reference modes and returns, exceptions, globals, namespaces, loops, switch fall-through, `break`, and `continue` remain covered. |
| Classes | Retained | Constructors, field initializers, methods, interfaces, virtual dispatch, destructors, single inheritance, access control, property accessors, and reference casts remain covered. |
| Built-in string | Intentional divergence | String remains a built-in `Value`; official AngelScript normally provides string through an add-on. |

## Embedding and compatibility boundary

| Capability | v0.4 status | Notes |
| --- | --- | --- |
| Engine/module/context lifecycle | Supported by mini API | Immutable module images, prepared-context snapshots, shared globals, stable IDs, and last-successful-image preservation remain stable. |
| Portable host global calls | Supported subset | `GenericCall` supports registered global functions and function-handle targets without native ABI bridges. |
| Host properties and object types | Deferred | Registered global properties, factories, behaviours, methods, object properties, value types, and richer declarations begin in v0.5. |
| Reflection and official-style facade | Deferred | Stable public metadata and `mini_as::compat` engine/module/context wrappers are v0.5 work. |
| Templates and standard add-ons | Deferred | Registered templates, array, dictionary, any/ref, initialization lists, indexing, and foreach are v0.6 work. |
| Advanced modules/runtime | Deferred | Imports, shared/external entities, incremental GC, context pooling, coroutines, serialization, variadics, and registered template functions remain v0.6 items. |

## Differential and build coverage

`MINI_AS_BUILD_COMPAT_TESTS=ON` pins the official engine to 2.38.0. The v0.4
corpus adds unchanged scripts for funcdefs, function handles, delegates,
non-capturing anonymous functions, child funcdefs, official weakref add-on
behavior, generated copy constructors, and deleted default/copy operations.
The runners compare completion state and return value; they do not compare
internal bytecode or ABI.

CI remains responsible for GCC, Clang, MSVC, and ASan/UBSan coverage. On the
current Windows workstation, local acceptance prioritizes the MSVC `/W4` build
and the validated GCC toolchain. Clang is used only with a correctly loaded
Visual Studio developer environment, while sanitizer enforcement remains in
the non-interactive CI/Linux job to avoid the local Windows crash-dialog path.

## Next alignment boundary

v0.5 starts the host-facing surface: registered global properties, reference
type factories and behaviours, object methods/properties, value types, richer
declaration registration, reflection, dynamic module functions, versioned
bytecode, debugging metadata, and the `mini_as::compat` facade. The native ABI
bridge remains intentionally out of scope.
