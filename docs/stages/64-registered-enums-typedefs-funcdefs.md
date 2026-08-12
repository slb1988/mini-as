# Stage 64: registered enums, typedefs, and funcdefs

The host can now contribute the three named declaration kinds that previously
existed only inside script sections:

```cpp
engine->RegisterEnum("HostColor");
engine->RegisterEnumValue("HostColor", "HostRed", 40);
engine->RegisterTypedef("HostScore", mini_as::DataType::Int());
engine->RegisterFuncdef("HostScore HostTransform(HostScore value)");
```

These are compiler registrations, not runtime wrappers. Before parsing a module,
the engine injects their names into the parser's type catalog. A registered typedef
therefore resolves directly to its primitive type, an enum retains its named int32
identity, and a funcdef parses as a function-handle type. The same signatures are
then seeded into `TypeChecker`, assigned stable `TypeId` values, and copied into the
immutable module image where required by bytecode execution.

Registered enum values use the existing constant-expression lookup and typed integer
constants. Registered funcdefs use the existing `FunctionHandle`, `CallHandle`, and
host-function descriptor paths, so a handle can target either a script function or a
portable `GenericCall` callback without a new calling convention. Host function,
method, factory, and property declarations now normalize registered aliases and enum
or funcdef names before validation.

Registration rejects duplicate type names across host object, enum, typedef, and
funcdef categories. Enum values must be non-empty and unique within their namespace;
typedefs remain limited to AngelScript primitive numeric and boolean types, matching
the current script typedef subset. Script declarations cannot shadow a registered
named type, and incompatible function-address assignments produce a compile-time
diagnostic.

The AngelScript 2.38.0 differential case registers the equivalent enum, enum values,
typedef, funcdef, and generic global function in the official engine. Both runners
execute the same script that stores an enum, binds a registered callback through a
funcdef handle, and invokes it.
