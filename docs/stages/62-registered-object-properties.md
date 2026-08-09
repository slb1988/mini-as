# Stage 62: registered object properties

Host reference types can now expose fields with the same script syntax as
ordinary class members:

```cpp
engine->RegisterObjectProperty(
    "HostRef", "int value",
    [](const mini_as::ObjectHandle& object) {
        return mini_as::Value(static_cast<HostRef*>(object.Get())->value);
    },
    [](const mini_as::ObjectHandle& object, mini_as::Value value) {
        static_cast<HostRef*>(object.Get())->value = value.As<std::int32_t>();
    });
```

Instead of embedding a native C++ byte offset, the teaching API uses portable
getter and setter callbacks. This preserves the repository's no-native-ABI
boundary while exposing the same AngelScript source-level `object.property`
model. A mutable declaration requires both callbacks; `const type name`
requires only a getter and rejects a setter.

Registered properties become host `FieldSignature` entries and stable pointers
in the engine-owned `TypeInfo`. The compiler deliberately reuses `LoadField`,
`StoreField`, and `MakeFieldReference`. The VM selects script storage or host
callbacks from the receiver's real type, so assignment, compound assignment,
increment/decrement, and `out`/`inout` copy-back all share the existing lvalue
machinery.

Compile-time checks reject writes and output references to const properties.
Runtime access validates receiver availability and callback value types.
Callback exceptions, wrong getter result types, null receivers, and setter
failures retain the source location of the field expression.

The differential runner registers a real offset-backed `int value` with
AngelScript 2.38.0 and an equivalent callback-backed property with mini_as, then
executes the same read/compound-write script. Object handles are supported as
property types when their referenced type is already registered. Registered
value types and reflection metadata remain later v0.5 features.
