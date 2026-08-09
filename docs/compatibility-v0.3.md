# mini_angelscript v0.3 compatibility matrix

## Baseline and scope

This matrix records the source-level subset aligned with AngelScript 2.38.0 at
the end of v0.3. It supersedes the v0.2 matrix for current behavior while
retaining the project's teaching boundaries: C++17, typed bytecode,
`std::variant<Value>`, portable `GenericCall`, and the existing `mini_as` API.
It does not claim SDK ABI, header, native calling-convention, or bytecode-file
compatibility.

“Supported subset” means the listed behavior is executable and tested, while a
specific official extension remains deferred. Differential cases run unchanged
under both engines and compare completion state and return value.

## Types, literals, and expressions

| Capability | v0.3 status | Notes |
| --- | --- | --- |
| Signed/unsigned integers | Supported | `int8`, `int16`, `int`, `int64`, `uint8`, `uint16`, `uint`, and `uint64` preserve width and signedness through values and conversions. |
| Floating point | Supported | `float` and `double` use distinct values and typed arithmetic opcodes. |
| Numeric literals | Supported subset | Official binary, octal, decimal, hexadecimal forms and implemented suffix combinations are decoded with range diagnostics. Exact official overload ranking for every exotic suffix combination is not claimed. |
| Bitwise, shifts, exponent | Supported | Integer bitwise/shift operators, `>>>`, compound forms, and typed `**` are implemented with located runtime errors. |
| Enums | Supported subset | Explicit and inferred integral values, enum constants, and duplicate checks are implemented. Full official enum conversion permissiveness is not enabled. |
| Typedefs | Supported subset | Aliases of built-in primitive types participate in declarations and overloads. Object/function typedef variants are deferred. |
| Namespaces | Supported subset | Nested declarations, qualified lookup, and `::` resolution are implemented for current entities. Namespace access masks and host configuration groups are deferred. |
| Operator overloads | Supported subset | Core unary, binary/reverse, comparison, assignment, increment, conversion, cast, and `opCall` methods dispatch virtually. `is` remains identity. `opIndex` belongs to the indexing stage. |
| Property accessors | Supported subset | Compact and explicit class/interface accessors, implicit `this`, read/write diagnostics, and numeric/string compound assignment are implemented. Global, indexed, and object-valued compound properties are deferred. |
| Built-in string | Intentional divergence | String remains a built-in teaching value; official AngelScript normally supplies it through an add-on. |

## Functions and statements

| Capability | v0.3 status | Notes |
| --- | --- | --- |
| Default arguments | Supported | Defaults are checked in declaration scope and materialized at call sites. |
| Named arguments | Supported | Positional/named ordering, duplicate names, unknown names, and overload selection are checked. |
| `in`, `out`, `inout` | Supported subset | Copy-in/copy-out works for locals, globals, fields, and reference-return lvalues. References are not stored inside ordinary `Value` objects. |
| Return references | Supported subset | Mutable and const reference returns can name globals and fields with sufficient lifetime. General temporary/reference lifetime analysis is deferred. |
| `try` / `catch` | Supported | Table-driven handlers catch arithmetic, null receiver, host, and called-script exceptions; nested handlers unwind to the nearest catch. Exception-info add-on helpers are not built in. |
| Existing control flow | Supported | `if`, three loop forms, `switch` fall-through, `break`, `continue`, conditional expressions, globals, `const`, and `auto` retain v0.2 behavior. |

## Classes and object lifecycle

| Capability | v0.3 status | Notes |
| --- | --- | --- |
| Script destructors | Supported subset | Finalizers are queued by the engine, run once at safe points, and avoid re-entering the VM from `Release`. Full official value-object destruction ordering is not claimed. |
| Single inheritance | Supported | One script base class plus interfaces, inherited field layout, base construction, `super`, and virtual method dispatch use stable type/function identities. |
| Access control | Supported | Public, private, and protected fields, methods, constructors, destructors, operators, and properties are checked against the declaring class. |
| Reference casts | Supported subset | Built-in hierarchy/interface `cast<T>`, custom `opCast`/`opImplCast`, and null-on-failed-cast semantics are implemented for script handles. Registered host reference types arrive later. |
| Value-object model | Partial | Script class instances are represented by reference-counted handles. Generated copy constructors and disabled copy/default operations are scheduled for v0.4. |
| Cycle collection | Teaching implementation | Reference counting plus stop-the-world trial deletion remains available. Incremental collection and official GC callbacks are v0.6 work. |

## Embedding and compatibility boundary

| Capability | v0.3 status | Notes |
| --- | --- | --- |
| Engine/module/context | Supported by mini API | Immutable module images, prepared-context snapshots, shared module globals, and failed-rebuild preservation are stable. |
| Host global functions | Supported subset | Portable `GenericCall` callbacks, located exceptions, and parameter copy-out are supported. Native ABI bridges remain explicitly out of scope. |
| Host types, properties, methods | Deferred | Official-style registration, reflection, and the `mini_as::compat` facade are v0.5 items. |
| Function objects | Deferred | Funcdefs, handles, delegates, lambdas, and weak references begin in v0.4. |
| Templates/add-ons/modules | Deferred | Arrays, dictionaries, initialization lists, indexing, imports, shared entities, coroutines, and serialization remain v0.6 work. |

## Differential and build coverage

`MINI_AS_BUILD_COMPAT_TESTS=ON` pins the official archive to 2.38.0. The corpus
now covers every v0.2 acceptance area plus the integer family, double, numeric
literals, bitwise/shifts, exponentiation, enums, typedefs, namespaces, default
and named arguments, reference parameters and returns, destructors, inheritance,
access control, reference casts, operator overloads, property accessors, and
try/catch.

The repository CI builds GCC, Clang, and MSVC, runs the pinned differential
suite, and includes ASan/UBSan jobs. Local feature acceptance requires the full
CTest suite, warning-clean compiler builds, and `git diff --check` before each
independent commit.

## Next alignment boundary

v0.4 begins with funcdef declarations and progresses through function handles,
delegates, captured anonymous functions, child funcdefs, weak references, and
generated/disabled copy operations. None of those are implied by the v0.3
status above.
