# mini_angelscript v0.2 compatibility matrix

## Baseline and scope

This matrix compares the language and object-model subset implemented by
`mini_angelscript` with AngelScript 2.38.0. It describes source-level behavior,
not SDK headers, native ABI calling conventions, bytecode compatibility, or
internal implementation details.

The optional compatibility suite downloads the fixed 2.38.0 release and runs
the same scripts with both engines. It compares process success and normalized
observable output. The v0.2 acceptance script combines module state, all three
loop forms, switch fall-through, compound assignment, constructor arguments,
instance methods, and virtual interface calls.

## Language and expression support

| Capability | v0.2 status | Notes |
| --- | --- | --- |
| Primitive values | Partial | `void`, `bool`, `int`, `float`, and the built-in teaching `string` value are available. The full signed/unsigned integer family and `double` are deferred. |
| Local declarations | Supported | Multiple declarators, optional initializers, lexical scope, and shadowing checks are implemented. |
| `const` variables | Partial | Local and module-global assignment protection is implemented. Full const-handle and const-method semantics are deferred. |
| `auto` declarations | Supported | A non-empty initializer is required and its checked expression type becomes the declared type. |
| Module globals | Supported | Initialization follows declaration order. Contexts prepared from the same successful module image share global slots. |
| Arithmetic, comparison, and logic | Supported subset | Operators for the current primitive set are typed and compiled to bytecode. Bitwise, shift, exponent, and the wider numeric conversion rules are deferred. |
| Assignment expressions | Supported subset | Plain and compound assignment work for implemented lvalues. Property accessors and general index lvalues are deferred. |
| Prefix/postfix increment | Supported | Prefix returns the updated value; postfix returns the previous value. Const and non-numeric targets are rejected. |
| Conditional expression | Supported | `condition ? left : right` is lazy and requires compatible branch types under the current conversion rules. |
| Constant expressions | Partial | A reusable evaluator handles the primitive literal/operator subset used by the compiler. Named constants and the complete official folding rules are deferred. |

## Statements and control flow

| Capability | v0.2 status | Notes |
| --- | --- | --- |
| `if` / `else` | Supported | Conditions use the current boolean conversion rules. |
| `while` | Supported | `break` targets loop exit and `continue` targets the condition. |
| `do` / `while` | Supported | The body executes once before the condition; `continue` targets the trailing condition. |
| `for` | Supported | Declaration/expression initialization, optional condition, and optional increment are implemented; `continue` targets the increment. |
| `switch`, `case`, `default` | Partial | Integer constant-expression cases, duplicate detection, one default, and official-style fall-through are implemented. String cases and the complete official constant-expression set are deferred. |
| `break` | Supported | Valid in loops and switches; use outside either construct is diagnosed. |
| `continue` | Supported | Valid in loops; use elsewhere is diagnosed. |
| Script exceptions | Not supported | `try`/`catch` is planned for v0.3. VM exceptions still carry source locations and stack traces. |

## Classes and interfaces

| Capability | v0.2 status | Notes |
| --- | --- | --- |
| Script classes and fields | Supported subset | Reference-counted script objects, declared fields, and field load/store are available. Access control, inheritance, and value classes are deferred. |
| Instance methods and `this` | Supported | Methods receive a hidden receiver local; unqualified field and method access resolve through implicit `this`. |
| Constructors | Supported subset | Default and overloaded constructors are selected by argument types. Destructors, copy constructors, default arguments, and access control are deferred. |
| Field initializers | Supported subset | Initializers run in declaration order for each new object. The implementation covers the v0.2 script-class subset, not all official construction and inheritance interactions. |
| Interfaces | Supported subset | A class may implement the current minimal interface form and is checked for required methods. Interface inheritance and advanced conversions are deferred. |
| Virtual interface calls | Supported | Bytecode stores a stable interface type/slot pair. The VM resolves the implementation from the receiver's real `TypeId`, so calls through interface handles dispatch dynamically. |
| Null handle failures | Supported | Null field, method, and virtual calls become located VM exceptions. |

## Embedding and lifecycle

| Capability | v0.2 status | Notes |
| --- | --- | --- |
| Engine / module / context flow | Supported by the native mini API | The existing RAII-oriented `mini_as` API remains the public interface. The official-style facade is deferred to v0.5. |
| Failed module rebuild | Supported | A failed build retains the last successful immutable `ModuleImage`. Prepared contexts hold a shared image snapshot and may finish the old version safely. |
| Stable metadata identities | Internal foundation | Types, functions, variables, and globals use stable IDs rather than relocatable container addresses. Public reflection is deferred. |
| Host global functions | Supported subset | Portable `GenericCall` callbacks are supported. Native ABI conventions are intentionally out of scope. |
| Host methods/properties/types | Not supported | Registration APIs are planned for v0.5. |
| Garbage collection | Supported teaching implementation | Reference counting and stop-the-world trial-deletion cycle collection remain available; incremental collection and official statistics callbacks are deferred. |

## Differential coverage

With `MINI_AS_BUILD_COMPAT_TESTS=ON`, the suite fixes the upstream download to
AngelScript 2.38.0 and currently covers:

- primitive arithmetic;
- multiple declarations, `const`, `auto`, and module globals;
- `for`, `while`, `do while`, `switch`, `break`, and `continue`;
- compound assignment, prefix/postfix increment, and conditional expressions;
- instance methods, overloaded constructors, field initializers, and virtual
  interface dispatch;
- one combined `v02_acceptance.as` scenario exercising the v0.2 feature set.

Build and run the differential suite with:

```powershell
cmake -S . -B build-compat -DMINI_AS_BUILD_COMPAT_TESTS=ON
cmake --build build-compat --config Debug
ctest --test-dir build-compat -C Debug --output-on-failure
```

## Known gaps beyond v0.2

The roadmap still excludes, until their later milestones, the wider numeric
family, enums, typedefs, namespaces, full parameter modes, inheritance,
destructors, operator overloads, properties, exceptions, function handles,
delegates, host object registration, reflection, templates, standard add-ons,
module imports, incremental GC, coroutines, and serialization.

The project does not target AngelScript SDK ABI or header compatibility, native
C/C++ calling-convention bridges, JIT compilation, or production-grade
optimization. Those omissions preserve the small typed-bytecode architecture
and do not count as v0.2 regressions.
