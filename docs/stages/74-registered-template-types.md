# Stage 74: registered template types

## Goal

Hosts can now register AngelScript-style template declarations and let modules
materialize stable closed types from script uses:

```cpp
engine->RegisterObjectType("Box<class T>");
engine->RegisterTemplateType("Pair<class K, class V>");
```

`RegisterObjectType` recognizes the official declaration spelling and forwards
to the explicit mini API. The implementation remains a portable reference-type
foundation; the next stage builds the array add-on on top of it.

## Parsing and canonical identity

The parser receives visible template names and arities before parsing. It accepts
primitive, object, handle, qualified, multi-parameter, and nested subtype forms,
including adjacent closers such as `Pair<Box<int>, float>`. Each use is normalized
to a canonical name such as `Pair<Box<int>,float>`.

The tokenizer still emits `>>` and `>>>` as shift tokens. Type parsing splits
those tokens only while consuming nested template closers, so expression shift
semantics are unchanged. Unknown templates, empty subtype lists, and arity
mismatches produce located compile diagnostics.

## Closed instances and validation

The engine lazily materializes each distinct closed instance before type
checking. A closed instance receives its own stable `TypeId` and `TypeInfo`, while
repeated uses return the same pointer. The definition and instances expose:

- template-definition versus template-instance state;
- the qualified template base name;
- declared parameter names on the definition;
- concrete `DataType` subtypes on an instance.

The same information is available through stable `TypeMetadata`. Namespace,
access-mask, and configuration-group controls are inherited from the template
registration.

An optional portable validator runs once when a closed instance is first seen:

```cpp
engine->RegisterTemplateType("NumericBox<class T>",
    [](const std::vector<mini_as::DataType>& types, std::string& reason) {
        if (types[0].IsNumeric()) return true;
        reason = "numeric subtype required";
        return false;
    });
```

A rejection fails the candidate module build at the template use location and
preserves the module's previous successful image.

## Host declarations and dynamic compilation

The generic declaration reader now understands qualified and nested closed
template types. Registered global callbacks may therefore accept or return a
closed template handle, still exclusively through `GenericCall`. Dynamic
function compilation can introduce additional closed instances without losing
the module's existing environment.

This stage does not yet register subtype-substituted template factories,
methods, properties, initialization-list behaviours, or storage. Those runtime
behaviours arrive with the array and later add-on stages rather than becoming VM
built-ins.

## Verification

Tests cover canonical nested parsing, `>>` splitting, arity errors, malformed
and duplicate declarations, validator rejection with a source location, stable
definition/instance reflection, null-handle bytecode and execution, closed
template types in `GenericCall` declarations, and new instances introduced by
dynamic compilation.

The differential case registers `HostBox<class T>` as an official 2.38.0
`asOBJ_TEMPLATE | asOBJ_NOCOUNT` reference type and runs the same primitive
specialization/null-handle script in both engines.
