# mini_angelscript

`mini_angelscript` is an independent C++17 teaching implementation of the core
ideas behind AngelScript. It is intentionally small enough to read in commit
order while still compiling a statically typed language to bytecode and running
it through an embeddable VM.

Implemented concepts:

- hand-written tokenizer and recursive-descent parser;
- one arena-owned AST node shape using first-child/next-sibling links;
- static types, structured control flow, namespaces, references, overloads, and exceptions;
- classes, interfaces, inheritance, funcdefs, delegates, closures, and weak references;
- typed bytecode, transactional module images, debug metadata, and a heap-backed VM;
- portable `GenericCall` host bindings, registered types/properties/methods, and reflection;
- registered templates plus array, dictionary, any, and ref add-ons;
- imports, shared/external entities, mixins, context pools, and cooperative coroutines;
- reference counting, incremental cycle detection, and live-state serialization.

## Build

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

See `docs/stages/` for the design notes attached to each stage and
`docs/compatibility-v0.6.md` for the final AngelScript 2.38.0 compatibility
report.

## Tutorial

```powershell
./build/Debug/tutorial_clone.exe ./examples/tutorial/script.as
```

The host uses the same conceptual flow as the official tutorial:

```cpp
auto engine = mini_as::CreateScriptEngine();
engine->RegisterGlobalFunction("void Print(string &in)", printCallback);
auto* module = engine->GetModule("tutorial");
module->AddScriptSection("script.as", source);
module->Build();
auto context = engine->CreateContext();
context->Prepare(module->GetFunctionByDecl("float calc(float, float)"));
context->SetArgFloat(0, 3.14f);
context->SetArgFloat(1, 2.71f);
context->Execute();
```

## Compatibility scope

The v0.2-v0.6 roadmap aligns a substantial source-level and embedding subset
with AngelScript 2.38.0. Optional differential tests execute the same scripts
in mini_angelscript and the official engine. String remains a built-in `Value`,
and all host calls use the portable generic interface.

This project is not ABI-, header-, or bytecode-compatible with the official
SDK. Native calling conventions, JIT integration, production optimization,
and hard-real-time runtime guarantees remain outside its teaching-oriented
scope. See `docs/architecture.md`, the stage notes, and the compatibility
reports for exact boundaries.
