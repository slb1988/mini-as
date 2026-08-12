# Stage 79: dictionary add-on

## Goal

The standard add-on set now includes a garbage-collected `dictionary` reference
type backed by the VM's existing heterogeneous `Value` representation:

```angelscript
dictionary@ values = dictionary();
values.set("answer", int64(42));

int64 answer;
if (values.get("answer", answer)) {
    foreach (auto value, auto key : values) {
        // value is dictionaryValue; key is string
    }
}
```

`RegisterScriptDictionary` requires the array template add-on, mirroring the
official dependency on `array<string>` for `getKeys()`.

## Registered surface

The object provides the official core shape:

- `set` and `get` overloads for `int64` and `double`;
- matching overloads for the current built-in `string` and `bool` values;
- `exists`, `isEmpty`, `getSize`, `delete`, `deleteAll`, and `getKeys`;
- `dictionaryValue` plus its official `int64 opConv()` numeric conversion;
- bracket reads through the registered `get(string)` protocol;
- `opForBegin/End/Next` and numbered value/key methods for `foreach`.

The storage itself is not restricted to these overloads: the C++ `Set` and
`Get` API retain arbitrary `Value` instances, including object and function
handles. The later wildcard/variadic stage will expose the official `?&in` and
`?&out` declarations to scripts; until then, unsupported script-side value
types fail overload resolution instead of being silently coerced.

## Runtime and GC

`RegisterObjectType` now has an optional garbage-collected flag for registered
reference types. Dictionary instances register with the engine collector,
enumerate object/function/capture references stored in their entries, and clear
all entries when a dead cycle is reclaimed.

Iteration uses a stable ordered key map and an unsigned hidden iterator. Values
are copied into `dictionaryValue`, while keys are returned as strings. Runtime
conversion failures remain located at the conversion or bracket expression.

No dictionary-specific opcode was added. Factories, methods, out-parameter
writeback, conversions, indexing, and foreach all use the existing typed host
call descriptors.

## Verification

Tests cover typed set/get and output writeback, `dictionaryValue` conversion,
bracket reads, key arrays, size/existence/deletion, foreach values and keys,
invalid overload diagnostics, located conversion failures, GC self-cycles,
host-call emission, and bytecode save/load with host-symbol rebinding.

The differential case runs the same numeric storage and foreach script through
the official AngelScript 2.38.0 scriptdictionary, scriptarray, and scriptstdstring
add-ons.
