# Repository guide for agents

## Project intent

`mini_angelscript` is a small, readable C++17 implementation of an
AngelScript-like language. AngelScript 2.38.0 is the semantic reference, but
this repository is a teaching implementation rather than an SDK clone.

Preserve these boundaries:

- Keep the typed bytecode VM and `std::variant`-backed `Value` model.
- Keep host calls portable through `GenericCall`; do not add native ABI calling
  conventions or platform assembly bridges.
- Keep the existing `mini_as` API compatible. Official-style APIs belong in a
  future `mini_as::compat` facade.
- Prefer understandable end-to-end implementations over optimizer complexity.
- Do not claim official ABI, header, or source compatibility.

## Compilation pipeline

The primary path is:

```text
script sections
  -> Tokenizer
  -> recursive-descent Parser and arena-owned AST
  -> TypeChecker and durable signatures
  -> BytecodeCompiler and typed opcodes
  -> immutable ModuleImage plus shared ModuleState
  -> ScriptContext and VirtualMachine
```

Important ownership rules:

- `ScriptModule` publishes `shared_ptr<const ModuleImage>` snapshots.
- A failed rebuild must preserve the last successful image and global state.
- `ScriptContext::Prepare` retains its image so a prepared context can finish
  after the module is rebuilt.
- Bytecode targets use stable `TypeId`, `FunctionId`, `GlobalId`, and
  `VariableId` values rather than container element pointers.
- Module globals are shared by contexts prepared from the same image.

Important compiler abstractions:

- `LValueRef` represents local, global, field, and eventually indexed storage.
  Extend it instead of implementing assignment separately for every operator.
- `CallableRef` describes script, host, method, and virtual calls.
- `FunctionSignature::Declaration()` is used as part of durable function keys;
  update all AST/signature construction sites when signature metadata changes.
- Default and named arguments are ordered and materialized at call sites.
- `in`, `out`, and `inout` use copy-in/copy-out. References are not stored
  inside `Value`; field receivers are snapshotted before a call.
- The reusable `ConstantExpressionEvaluator` is the common path for enum values,
  switch cases, and future compile-time expressions.

## Source map

- `include/mini_as/core.hpp`, `src/core.cpp`: types, values, diagnostics.
- `include/mini_as/tokenizer.hpp`, `src/tokenizer.cpp`: tokens and lexing.
- `include/mini_as/parser.hpp`, `src/parser.cpp`: AST and grammar.
- `include/mini_as/type_checker.hpp`, `src/type_checker.cpp`: symbols, overload
  resolution, type rules, and durable signatures.
- `include/mini_as/bytecode.hpp`, `src/bytecode.cpp`: opcodes, compiler,
  disassembler, module bytecode metadata.
- `include/mini_as/vm.hpp`, `src/vm.cpp`: execution, calls, suspension, and
  exception locations.
- `include/mini_as/coroutine.hpp`, `src/coroutine.cpp`: cooperative scheduling,
  yield registration, cancellation, and completed-result retention.
- `src/state_io.cpp`: versioned live module-state archives, object/capture graph
  identity, schema validation, and transactional restore.
- `include/mini_as/engine.hpp`, `src/engine.cpp`: public Engine/Module/Context
  lifecycle and module snapshots.
- `include/mini_as/generic.hpp`, `src/generic.cpp`: portable host registration
  declaration parser and `GenericCall`.
- `include/mini_as/object.hpp`, `src/object.cpp`: intrusive handles, script
  objects, type information, and cycle collection.
- `tests/compat/`: differential runners and scripts for AngelScript 2.38.0.
- `docs/stages/`: one design note per delivered stage.

`src/interpreter.cpp` is retained for the early teaching stages. New language
features normally need to work through the typed compiler and VM path; do not
mistake the legacy interpreter for the production execution path.

## Build and test

Normal local build:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

AngelScript 2.38.0 differential suite:

```powershell
cmake -S . -B build-compat -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DMINI_AS_BUILD_COMPAT_TESTS=ON
cmake --build build-compat --parallel
ctest --test-dir build-compat --output-on-failure -R '^compat_'
```

An AngelScript source repository is available at `D:\Github\angelscript2`, but
its current working tree identifies itself as `2.39.0 WIP` even though its Git
history contains tag `v2.38.0`. Use it for source-history inspection with an
explicit tag; do not point the differential build at the current working tree.
CMake downloads the hash-pinned 2.38.0 archive declared in `CMakeLists.txt`.
The suite checks script output, execution state, and normalized diagnostics,
not bytecode or ABI. The official runner enables
`asEP_ALLOW_UNSAFE_REFERENCES` so primitive `&inout` cases are executable.

CI builds GCC, Clang, and MSVC, runs the differential corpus against exactly
AngelScript 2.38.0, and has an ASan/UBSan job. Keep
`-Wall -Wextra -Wpedantic` and MSVC `/W4` free of new warnings.

On this Windows workstation, prioritize the MSVC `/W4` build and the locally
validated GCC toolchain. Do not invoke local Clang in unattended work unless
the user explicitly requests it: the installed path can open a blocking crash
dialog during C++ exception tests. Leave Clang and ASan/UBSan enforcement to
the non-interactive CI/Linux jobs.

Test placement:

- tokenizer/parser shape tests: `tests/test_tokenizer.cpp`,
  `tests/test_parser.cpp`;
- type and compiler/opcode tests: `tests/test_types.cpp`,
  `tests/test_bytecode.cpp`;
- VM mechanics and exception locations: `tests/test_vm.cpp`;
- public API and end-to-end scripts: `tests/test_engine.cpp`;
- host bridge behavior: `tests/test_generic.cpp`;
- official semantic comparisons: `tests/compat/cases/*.as` plus a CMake test.

## Feature workflow

The alignment roadmap uses one independently green commit per feature. A
feature normally includes, in the same commit:

1. tokenizer and parser support;
2. type rules and negative diagnostics;
3. bytecode and VM behavior;
4. host bridge or public API changes when applicable;
5. at least one successful script and one illegal-use test;
6. exception location coverage when runtime failure is possible;
7. a differential case when AngelScript 2.38.0 supports the same semantics;
8. a numbered `docs/stages/NN-*.md` note.

Use `feat:`, `fix:`, `refactor:`, `test:`, or `docs:` commit subjects. Put
cross-cutting refactors and unrelated fixes in separate commits. Before a
feature commit, run the complete unit suite, differential suite when configured,
and `git diff --check`.

Do not stage generated build directories or unrelated user changes. Inspect
`git status --short` before staging and add intended paths explicitly.

## Current alignment position

The completed stage notes are authoritative. The latest alignment stage is:

- Stage 90 suspended-context serialization.

The next planned item is Stage 91, variadic arguments.
Confirm the latest
git history and `docs/stages/` before choosing the next stage number.

## Common pitfalls

- Adding a token requires updating both the keyword/operator scanner and
  `TokenName` in matching enum order, plus the serialized AST token bounds in
  `bytecode_io.cpp`.
- Adding `FunctionSignature` fields requires updating script functions, class
  methods, interfaces, constructors, host declarations, temporary AST keys,
  overload checks, virtual slot matching, and aggregate initializers.
- Call arguments may be positional, named, or omitted defaults. Always operate
  on parameter-ordered arguments before emitting bytecode or copy-out logic.
- Methods reserve local slot zero for implicit `this`; script function
  parameters start at zero.
- `StoreField` leaves the stored value on the operand stack, unlike local and
  global stores. Emit an explicit `Pop` when the expression value is unwanted.
- VM exceptions must retain the source location of the failing instruction and
  must not publish partial `out`/`inout` updates.
- Registered reference type factories use `Type@ f(...)` declarations and
  `GenericCall`; `RefObject` plus `ObjectHandle` remain the mandatory intrinsic
  add-reference/release behaviours.
- Registered object method callbacks receive `this` through
  `GenericCall::GetObject()`; keep it separate from explicit script arguments.
- Registered object properties reuse field opcodes and carry stable callback
  pointers in `TypeInfo`; preserve getter/setter type validation and constness.
- Registered value types live in `HostValueStorage` and copy through `std::any`.
  Const callbacks receive the value through `GenericCall::GetObjectValue()`;
  reject mutable methods until receiver writeback is implemented.
- Registered enums, typedefs, and funcdefs must be injected into `Parser` before
  parsing and into `TypeChecker` before `Check`; otherwise their surface spelling
  degrades to an object type and later bytecode metadata will be inconsistent.
- `TypeMetadata` addresses are stable because the engine owns them in a deque.
  Refresh records in place, and publish script metadata only after the complete
  build including global initialization succeeds.
- `FunctionMetadata` follows the same stable deque and atomic publication rule.
  Host functions publish at registration; script globals and methods publish
  only after the complete build succeeds. Keep removal semantics in the later
  explicit function-removal stage.
- `GlobalMetadata` is also deque-backed, but public lookup is module-scoped and
  must first confirm membership in the current `ModuleImage`. Do not include
  registered host properties in module-global counts or index lookup.
- Dynamic function compilation copies the current bytecode image and shares its
  `ModuleState`. Preserve old callable descriptor indices, then rebase all six
  callable-using opcodes in newly compiled functions when appending descriptors.
- Successful images retain typed `SyntaxTree` definitions so later incremental
  calls can compile omitted default-argument expressions in the defining scope.
- Dynamic images are retained in `ScriptModule::dynamicImages_` so raw pointers
  returned by earlier compilations survive later incremental compilations. A
  detached function remains executable but must not enter module lookup or later
  compilation scope.
- Removing a function adds its id to `ModuleImage::removedFunctions` and removes
  only its compilation signature. Keep retired bytecode in subsequent images so
  old call descriptors continue resolving the original id.
- Bytecode archives never reuse serialized numeric ids directly. Remap every
  function/type/global reference for the target engine, rebind host pointers,
  run the global initializer in a candidate state, and publish only on success.
- Persist typed definition trees with bytecode so post-load incremental calls
  retain default-argument expressions. Live global/object state is deliberately
  outside bytecode and belongs to the later serialization stages.
- Debug stack level zero is the current function and higher levels walk callers.
  Keep named-variable metadata aligned with lexical compiler scopes, hide
  compiler-generated slots, unwrap captured cells for inspection, and snapshot
  all frames before exception cleanup. Debug values are copies, not VM addresses.
- Any serialized `BytecodeFunction` metadata change requires a bytecode format
  version bump plus load-time bounds validation. Stage 71 uses format version 2.
- `mini_as::compat` mirrors official integer constants and call flow while
  retaining `unique_ptr` ownership. It is not an SDK header/ABI shim: keep
  native calling conventions and `AddRef`/`Release` out, map new operations to
  existing mini APIs, and leave a `Native()` escape hatch for partial coverage.
- Host registrations capture the current default namespace, access mask, and
  configuration group. Filter every registration category before parser/type
  injection; an access check after bytecode emission is too late. Group removal
  marks stable deque entries inactive so unrelated live bytecode pointers do
  not dangle, and must refuse removal while a live module environment depends
  on the group.
- Registered template definitions are parser inputs, while closed instances are
  materialized before type checking with their own stable `TypeId`. Keep nested
  `>>` splitting confined to type parsing, inherit host registration controls,
  and preserve canonical subtype spelling in reflection and host declarations.
- The array add-on is a GC-tracked registered template, not a VM primitive.
  Template instance callbacks register closed factories/methods, inherit the
  definition's host controls, and must also be recreated before bytecode host
  symbol remapping. Keep initialization lists and index syntax in their own
  language stages.
- Initialization lists are contextual expressions lowered through a zero-arg
  host factory plus `insertLast(T)`. Preserve this registered-type protocol and
  ordinary `CallHost` lowering; do not add array-specific VM instructions.
- Indexing uses registered `get(uint)` / value-returning `set(uint,T)` methods.
  `LValueRef::Index` caches receiver and index for compound/increment lowering
  so side effects run once; preserve ordinary assignment and postfix results.
- Foreach resolves exact `opForBegin`, `opForEnd`, `opForNext`, and
  `opForValue`/numbered value signatures. The range and iterator live in hidden
  locals; keep `continue` targeting next rather than the end condition.
- Dictionary depends on the array template registration for `array<string>`.
  It stores arbitrary C++ `Value`s, but scripts currently see typed overloads;
  preserve that boundary until wildcard parameters are implemented. Registered
  GC reference types must opt in through `RegisterObjectType(name, true)`.
- Registered value types that own strong object references must use
  `Value::ManagedHostValue` and provide enumerate/clear callbacks. Containers
  and script fields delegate GC traversal to `Value`; do not inspect the
  variant and assume every object-typed value is an `ObjectHandle`.
- Register the `ref` add-on before `any` when both are needed. The current
  script surface exposes typed `any` overloads and explicit C++ `MakeScriptRef`
  / `GetScriptRef` bridges; wildcard parameters and generic script casts remain
  deferred to the variadic-argument stage.
- Imported declarations are compiled into `CallableKind::ImportedFunction` and
  bound in mutable `ModuleState`; never splice source-module bytecode into the
  consumer image. Cross-module VM frames must save and restore bytecode, global
  state, image ownership, and finalizer context together. An unbound import is
  a located runtime exception, while binding requires an exact global-function
  callable shape (the implementation name may differ for manual binding).
- Bytecode persists imported signatures and source-module names but deliberately
  does not persist live bindings; load produces an unbound module that the host
  must bind again.
- `shared` is contextual, not a globally reserved identifier: recognize it only
  as a top-level entity modifier so legacy variables named `shared` continue to
  parse like official AngelScript. Shared class/interface/enum/funcdef/function
  definitions are structurally fingerprinted in the engine and published only
  after a successful build. Shared code may use host registrations and other
  shared entities, but never module globals or non-shared script entities.
- Bytecode format version 6 persists shared/external flags and mixin AST metadata. Definition fingerprints
  can be reconstructed from archived syntax trees. Shared function/method IDs
  use engine-wide keys; ordinary script function IDs remain module-scoped.
- External shared declarations reuse canonical signatures from an earlier
  successful engine build. Keep their bytecode body-free and resolve direct,
  virtual, and destructor targets by stable `FunctionId`; retain canonical ASTs
  for field initializers and default arguments. Loading a defining module must
  republish the canonical registry before an external consumer is built.
- Mixin declarations are non-types. Expand their fields, methods, and interface
  requirements into each including class after parsing, retain original source
  locations, and compile copied methods in the target class context. Explicit
  class members win; mixin methods override base methods; conflicting inherited
  fields and their initializers are omitted.
- Incremental GC detection is generation guarded. Every tracked-object
  registration, removal, `AddRef`, and `Release` invalidates an in-progress
  graph classification; collector-owned temporary holds suppress those
  notifications. Keep garbage destruction atomic after the four budgeted
  detection phases so clearing one cycle edge cannot invalidate raw candidates.
- GC statistics distinguish newly registered objects, ordinary reference-count
  destruction, and objects detected in cycles. Invoke circular-reference
  callbacks after classification but before acquiring collector holds or
  clearing references; callbacks are inspection-only and must receive stable,
  read-only object and type pointers.
- Context pools use the paired `RequestContext` / `ReturnContext` callback
  contract. A return callback owns the raw pointer and should call `Unprepare`
  before storing it under `unique_ptr`; active and suspended contexts must be
  finished or aborted first. Unprepare keeps context configuration such as the
  line callback but releases execution state and the retained module image.
- `CoroutineScheduler` is single-threaded and must die before its engine. A
  script `yield()` only requests suspension; the statement completes and the
  next bytecode suspension cue returns control. Defer cancellation of the
  currently active coroutine until `Execute()` returns, then send every terminal
  context through `ReturnContext()` so configured pools remain authoritative.
- Live state archives are separate from bytecode archives. Serialize only
  non-host module globals, assign IDs to strongly reachable objects and captured
  cells before writing payloads, and resolve function/type descriptors by stable
  spelling on load. Build the entire candidate graph before swapping roots;
  failed loads must clear candidate edges without touching published state.
- Restored `ScriptObject`s are constructed without finalizers during validation,
  then receive the current module image/state binding immediately before commit.
  Registered host reference/value types require an explicit codec; never persist
  raw pointers or opaque `std::any` payload bytes.
- Suspended-context archives share one object/captured-cell graph across all
  involved module globals, VM stack values, current locals, and caller frames.
  Resolve functions by module/declaration/object type and validate code length
  plus the next opcode/source cue before installing a saved PC. Rebuild module
  owners, finalizer state, and safe-point callbacks from target images; callbacks
  and pool ownership are host configuration and are never archived.
- Registered variadics are generic-call only. The final signature parameter is
  a repeated prototype, at least one tail argument is required, named arguments
  target only the fixed prefix, and a viable fixed-arity overload always wins.
  `?` is registration-only and must be `?&in ...` or `?&out ...`; preserve each
  wildcard output lvalue's concrete type through callback validation. Every
  call site stores its actual argument count in `CallableRef`. Bytecode format
  version 7 persists `FunctionSignature::variadic`.
- Registered function templates are generic-call only and are instantiated from
  explicit script type arguments before type checking. Open definitions remain
  parser/metadata inputs; only closed instances enter ordinary overload lookup,
  and each instance receives a stable engine-wide `FunctionId`. Preserve host
  namespace, access-mask, and configuration-group controls on every instance.
- Template function substitution currently accepts direct `T` and `T@`
  placeholders. Type inference, nested forms such as `array<T>`, defaults, and
  specializations are intentionally unsupported. `GenericCall` exposes the
  concrete list through `GetTemplateArgCount()` / `GetTemplateArgType()`.
  Bytecode format version 8 persists template signatures and call-site type
  arguments; recreate instances before remapping archived host symbols.
- MinGW GCC debug builds can exceed the PE/COFF section limit as `engine.cpp`
  grows. Keep `-Wa,-mbig-obj` enabled for that compiler; MSVC remains the
  primary Windows validation toolchain and local GCC is the secondary one.
- Preserve the last successful module image on parser, type-check, bytecode, or
  global-initializer failure.
- Do not edit compatibility expectations merely to make mini and official
  outputs agree; first determine which side differs from AngelScript 2.38.0.
