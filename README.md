# mini_angelscript

`mini_angelscript` is an independent C++17 teaching implementation of the core
ideas behind AngelScript. It is intentionally small enough to read in commit
order while still compiling a statically typed language to bytecode and running
it through an embeddable VM.

Implemented concepts:

- hand-written tokenizer and recursive-descent parser;
- one arena-owned AST node shape using first-child/next-sibling links;
- static types, lexical scopes, function predeclaration, and `int -> float` conversion;
- typed bytecode, disassembly, jump backpatching, and a heap-backed stack VM;
- Engine/Module/Context ownership, script calls, recursion, suspension, and stack traces;
- portable GenericCall host bindings parsed from declaration strings;
- host reference objects, script class fields, intrusive handles, and interface validation;
- reference counting plus stop-the-world trial-deletion cycle collection.

## Build

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

See `docs/stages/` for the design notes attached to each stage.

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

## Language subset

The parser accepts `void`, `bool`, `int`, `float`, `string`, object handles,
locals, blocks, assignment, arithmetic/comparison/logical expressions,
`if/else`, `while`, functions, forward calls, recursion, classes with fields,
default factories, and minimal interfaces. Strings format primitive operands
when used with `+` to support the tutorial.

This is not ABI- or source-compatible with the full AngelScript SDK. Native ABI
bridges, inheritance, templates, exceptions in script, delegates, JIT,
serialization, incremental GC, and the production optimizer are deliberately
out of scope. See `docs/architecture.md` and the stage notes for exact design
tradeoffs and upstream comparisons.
