# Stage 61: registered object methods

Host reference types can now publish instance methods through the portable
generic bridge:

```cpp
engine->RegisterObjectMethod(
    "HostRef", "int get() const",
    [](mini_as::GenericCall& call) {
        const auto* self = static_cast<const HostRef*>(call.GetObject().Get());
        call.SetReturnInt(self->Get());
    });
```

The declaration parser accepts the official trailing `const` qualifier and
retains it in durable declarations. Registered methods live only on their owner
type; they are not accidentally visible as global functions. Overloads use the
same conversion, named-argument, and reference-parameter rules as script
methods and global host functions.

Host method metadata is attached to the Stage 60 host `ClassSignature` during
module compilation. Calls emit `CallHost` with a `HostMethod` descriptor that
contains stable `FunctionId` and `TypeId` values. The VM pops the receiver
separately from explicit arguments, validates its runtime type, and exposes it
through `GenericCall::GetObject()`. `out` and `inout` values continue through
the common copy-in/copy-out path.

Null receivers, callback exceptions, incompatible return values, and invalid
output writes remain located VM exceptions. Return references are rejected at
registration until host property storage can provide a durable reference
target. Delegates to registered methods are also diagnosed explicitly instead
of entering the script-only virtual dispatch path.

The implementation follows AngelScript 2.38.0's [registered object method
model](https://www.angelcode.com/angelscript/sdk/docs/manual/doc_reg_objmeth.html)
without adding native calling conventions. The differential runner registers
`int HostRef::get() const` in both engines and executes the same script.
