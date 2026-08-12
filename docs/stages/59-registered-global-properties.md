# Stage 59: registered global properties

Host applications can now expose live primitive and string storage to scripts
through the existing teaching-oriented engine API:

```cpp
mini_as::Value counter(std::int32_t{40});
engine->RegisterGlobalProperty("int hostCounter", &counter);
```

The declaration parser accepts the AngelScript spelling `type name` and
`const type name`. Registered properties enter type checking before module
globals, so scripts use the normal global lvalue path and receive the same type
and const-assignment diagnostics. A script global cannot shadow a registered
property with the same name.

Each property receives an engine-stable `GlobalId`. Module images link that id
to an entry in the engine's stable registration deque; bytecode never embeds
the host pointer. Ordinary loads, stores, compound assignments, and
`out`/`inout` reference writeback all pass through the shared VM global access
helpers. Reads observe host changes immediately and writes update host storage
immediately, across every context and module that references the property.

The host owns the registered `Value` and must keep it alive while the engine,
its modules, or prepared contexts can use it. Registration verifies the initial
type. The VM verifies it again at every access so accidental replacement with a
different `Value` type becomes a located script exception instead of corrupting
state.

This stage intentionally supports numeric values, `bool`, and the repository's
built-in `string`. Object properties depend on the later registered object type
and property work. The future `mini_as::compat` facade will add official-style
integer result codes; the existing `mini_as` API continues to return `bool`.

The differential case registers the same `int hostCounter` storage in both
runners and executes an unchanged AngelScript script against version 2.38.0.
