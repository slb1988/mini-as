# mini_angelscript v0.6 AngelScript 2.38.0 compatibility report

## Baseline and result

This report records the end of the planned v0.2-v0.6 alignment against
AngelScript 2.38.0. Every roadmap feature has an independent implementation
commit and a matching stage note under `docs/stages/`. The v0.6 milestone adds
templates and standard add-ons, advanced module composition, incremental
garbage collection, context orchestration, live-state serialization, variadic
host calls, and registered template functions to the retained v0.2-v0.5
surface.

mini_angelscript remains an independent C++17 teaching implementation. It uses
typed bytecode, `std::variant<Value>`, stable internal ids, immutable module
images, and the portable `GenericCall` host boundary. Compatibility means
matching observable source behavior for the documented subset; it does not
mean SDK header, ABI, native calling-convention, or bytecode compatibility.

## Templates, containers, and expressions

| Capability | v0.6 status | Notes |
| --- | --- | --- |
| Registered template types | Supported subset | Host template definitions create stable closed `TypeId` instances on demand and expose canonical subtype metadata. Multiple template parameters, script-defined templates, and the complete SDK template-validation callback contract are not claimed. |
| Array add-on | Supported subset | `array<T>` is a registered GC-aware reference template with factories and ordinary methods, rather than a VM primitive. The commonly used construction, length, mutation, index, and iteration paths are covered. |
| Initialization lists | Supported subset | Contextual lists lower through a zero-argument factory and `insertLast(T)`. The implementation targets registered sequence types and does not reproduce every official list pattern or nested-list rule. |
| Indexing expressions | Supported subset | Registered `get(uint)` and value-returning `set(uint,T)` implement reads and lvalues, including assignment, compound assignment, and prefix/postfix update with single evaluation of receiver and index. |
| `foreach` protocol | Supported subset | `opForBegin`, `opForEnd`, `opForNext`, and one or more `opForValue` methods drive typed iteration; `break` and `continue` preserve the official loop destinations. |
| Dictionary add-on | Supported subset | A GC-aware registered dictionary supports typed set/get, existence, deletion, clearing, and key enumeration. The internal container can hold any `Value`, while the current script API exposes typed overloads. |
| `any` and `ref` add-ons | Supported subset | Registered GC-aware wrappers preserve strong references and provide typed script operations plus explicit C++ bridges. The full official wildcard cast surface is not reproduced. |
| Registered template functions | Supported subset | Global functions and object methods use explicit type arguments, stable cached instances, ordinary overload resolution, reflection, function handles, and `GenericCall` template-argument inspection. Type inference, nested substitutions such as `array<T>`, defaults, specializations, and script-defined template functions remain unsupported. |

## Modules and shared entities

| Capability | v0.6 status | Notes |
| --- | --- | --- |
| Imported functions | Supported subset | Imported declarations bind to exact callable shapes in another module's mutable state. Calls keep both module images alive, and unbound imports fail with a source-located exception. Live bindings are intentionally not serialized in bytecode. |
| Shared entities | Supported subset | Shared classes, interfaces, enums, funcdefs, functions, and methods are structurally fingerprinted and canonicalized engine-wide. Shared code cannot depend on module globals or non-shared script entities. |
| External shared entities | Supported subset | Body-free external declarations reuse a previously published shared definition and stable ids. Loading a defining bytecode image republishes its canonical shared registry before consumers build. |
| Mixin classes | Supported subset | Mixins expand fields, methods, and interface requirements into target classes while preserving source locations and deterministic conflict rules. Mixins remain declarations, not runtime types. |

The v0.5 transactional module model remains intact: failed builds and failed
bytecode/state loads do not replace the last successful image or published
state, and already prepared contexts retain the snapshot they own.

## Runtime, GC, and serialization

| Capability | v0.6 status | Notes |
| --- | --- | --- |
| Incremental cycle detection | Supported subset | Trial-deletion graph classification is split into budgeted, generation-guarded phases. Destruction remains an atomic final phase so callbacks and edge clearing cannot invalidate live candidate pointers. |
| GC statistics and callbacks | Supported subset | Statistics distinguish registrations, reference-count destruction, and detected cycles. Circular-reference callbacks run on stable read-only candidates before collector holds and edge clearing. |
| Context pooling | Supported | Paired request/return callbacks own reusable contexts; `Unprepare` releases execution state and module snapshots while retaining host configuration such as line callbacks. |
| Cooperative coroutines | Supported subset | A single-threaded scheduler runs contexts that suspend through a registered `yield()`, resumes them fairly, handles cancellation, and returns terminal contexts through the configured pool. Threaded scheduling and native stackful coroutines are out of scope. |
| Module globals and object graphs | Supported subset | The private `MASS` archive preserves non-host globals, strongly reachable script objects, captured cells, and supported registered host values through explicit codecs. It never writes raw pointers or opaque `std::any` bytes. |
| Suspended contexts | Supported subset | Archives preserve VM values, program counters, locals, caller frames, shared object identity, and relevant module globals. Functions and types resolve by stable declarations in target images; host callbacks, import bindings, pools, and external resources remain host configuration. |
| Variadic arguments | Supported subset | Registered globals, factories, methods, and host function handles repeat a final fixed or wildcard prototype. At least one tail argument is required, and wildcard out values are type-checked before lvalue writeback. Script-defined variadics and native ABI varargs are excluded. |

Bytecode and live state are deliberately separate formats. The current private
bytecode format is `MASB` version 8; it persists variadic and template-function
metadata and rebuilds required registered instances before host-symbol
remapping. It is not interchangeable with official AngelScript bytecode.

## Retained language and embedding surface

All documented v0.2-v0.5 behavior remains covered: globals and structured
control flow; integer and floating-point families; literals and operators;
enums, typedefs, namespaces, arguments and references; exceptions; classes,
interfaces, inheritance, access control, constructors/destructors, casts,
operators and properties; funcdefs, handles, delegates, closures and weak
references; host properties/types/methods; stable reflection; dynamic function
compilation/removal; debug inspection; host registration controls; and the
opt-in `mini_as::compat` facade.

The original `mini_as::ScriptEngine`, `ScriptModule`, and `ScriptContext` API
remains available without a semantic replacement. `mini_as::compat` continues
to be a convenience facade, not a clone of the official C++ interfaces.

## Differential and build acceptance

`MINI_AS_BUILD_COMPAT_TESTS=ON` builds the exact AngelScript 2.38.0 source and
compares normalized output, completion state, return values, and selected
diagnostics. The final configured suite contains 69 CTest entries: two native
test programs and 67 differential cases. It includes unchanged-source coverage
for every source-level v0.2-v0.6 feature that has a meaningful official
counterpart; private bytecode, GC internals, scheduling policy, and archive
formats are verified by native tests instead.

Local final acceptance passed:

- MSVC 2022 `/W4`: build, 381 unit tests, and both configured CTest entries.
- MinGW GCC 13 `-Wall -Wextra -Wpedantic`: build and all 69 CTest entries,
  using the hash-pinned AngelScript 2.38.0 source archive.
- No local Clang process was run, avoiding the known Windows crash-dialog path;
  Clang and Linux ASan/UBSan remain CI responsibilities.

## Remaining official capabilities and intentional boundaries

The completed roadmap does not claim all of AngelScript 2.38.0. The most
important remaining gaps are:

- official SDK headers, source or binary ABI compatibility, native C/C++ call
  conventions, platform assembly bridges, JIT, and production optimization;
- complete template semantics, including deduction, nested template-function
  substitution, specialization, and script-declared templates;
- the entire standard add-on surface and every array/dictionary/any/ref edge
  case, including all wildcard conversions and serialization callbacks;
- full official reflection interfaces, behaviour registration, user-data
  slots, configuration APIs, and exact result-code distinctions;
- lock-free multi-threaded execution, hard-real-time or production-scale GC,
  and platform-independent serialization of arbitrary host resources;
- peripheral file, network, date/time, and similar libraries.

These boundaries preserve the repository's main purpose: a readable engine in
which parsing, typing, bytecode, object lifetime, embedding, and runtime state
can be followed end to end while a substantial AngelScript 2.38.0 script subset
runs without source changes.
