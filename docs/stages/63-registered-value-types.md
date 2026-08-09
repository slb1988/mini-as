# Stage 63: registered value types

Hosts can now expose copyable C++ values without turning them into reference-counted
objects:

```cpp
struct HostNumber { int value = 42; };

engine->RegisterValueType(
    "HostNumber",
    mini_as::Value::HostValue("HostNumber", HostNumber{}));
```

`HostValueStorage` keeps the C++ object in `std::any`. Copying a script `Value`
therefore invokes the contained C++ type's copy operation and gives locals, globals,
arguments, and return values independent storage. The registered default value is
used for default construction; construction from one value of the same type provides
copy construction. Other constructor forms are rejected with a located diagnostic.

Registered values participate in assignment, module globals, pass/return by value,
and `out`/`inout` copy-back. A host can pass one directly to a prepared context with
`SetArgValue`. Generic callbacks inspect arguments with `AsHostValue<T>()`.

Const methods are also available through the portable host method bridge. Their
receiver is exposed as `GenericCall::GetObjectValue()`:

```cpp
engine->RegisterObjectMethod("HostNumber", "int get() const",
    [](mini_as::GenericCall& call) {
        call.SetReturnInt(call.GetObjectValue().AsHostValue<HostNumber>().value);
    });
```

Mutable value-type methods are deliberately rejected for now. A method call currently
receives a copied value; allowing mutation before the compiler can write that receiver
back would silently lose changes. Registered value-type properties have the same
dependency and remain unsupported.

The differential case registers AngelScript 2.38.0 construction, destruction, copy
construction, assignment, and a const method, then runs the same default/copy/method
script in both engines. mini_as continues to use `GenericCall` only and does not add a
native ABI calling convention.
