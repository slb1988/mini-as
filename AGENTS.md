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

The compatibility build downloads the archive pinned in `CMakeLists.txt` and
checks script output, execution state, and normalized diagnostics. It does not
compare bytecode or ABI. The official runner enables
`asEP_ALLOW_UNSAFE_REFERENCES` so primitive `&inout` cases are executable.

CI builds GCC, Clang, and MSVC, runs the differential corpus against exactly
AngelScript 2.38.0, and has an ASan/UBSan job. Keep
`-Wall -Wextra -Wpedantic` and MSVC `/W4` free of new warnings.

On this Windows workstation, prioritize the MSVC `/W4` build and the locally
validated GCC toolchain. Run Clang locally only after loading the Visual Studio
developer environment. Do not use the interactive Windows Clang sanitizer
configuration: its runtime can open a blocking crash dialog during C++
exception tests. Leave ASan/UBSan enforcement to the non-interactive CI/Linux
job unless a local sanitizer toolchain is known to run cleanly.

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

- Stage 68 dynamic compilation of one added or detached global function against
  a saved module compilation environment and immutable runtime snapshot.

The next planned feature is dynamic function removal. Confirm the latest
git history and `docs/stages/` before choosing the next stage number.

## Common pitfalls

- Adding a token requires updating both the keyword/operator scanner and
  `TokenName` in matching enum order.
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
- Preserve the last successful module image on parser, type-check, bytecode, or
  global-initializer failure.
- Do not edit compatibility expectations merely to make mini and official
  outputs agree; first determine which side differs from AngelScript 2.38.0.
