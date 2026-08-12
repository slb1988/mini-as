# Stage 78: foreach operator protocol

## Goal

`foreach` now consumes an object through the AngelScript 2.38.0 iterator
operator protocol instead of recognizing arrays specially:

```angelscript
foreach (auto value, auto index : values) {
    if (index == 1) continue;
    total += value;
}
```

The tokenizer reserves `foreach`, the parser records one or more typed or
`auto` iteration variables followed by the range expression and body, and the
type checker infers each `auto` variable from its value operator.

## Operator contract

A range must provide exact method signatures for:

- `Iterator opForBegin()`;
- `bool opForEnd(Iterator)`;
- `Iterator opForNext(Iterator)`;
- `Value opForValue(Iterator)` for a single item, or numbered
  `opForValue0`, `opForValue1`, ... methods for one or more items.

The unnumbered single-value form is preferred, matching the official compiler.
Return values are converted to explicitly typed iteration variables. Missing,
mis-typed, or inaccessible operators are compile errors. Both script methods
and registered generic host methods use the normal callable dispatch path, so
virtual script methods remain virtual.

The array add-on registers the official five-method shape: an unsigned index
iterator, `opForValue0` for the element, and `opForValue1` for its index.

## Lowering and control flow

The compiler evaluates the range expression once and stores both it and the
iterator in hidden locals. Each pass performs the equivalent of:

```angelscript
for (auto range = expression, auto iterator = range.opForBegin();
     !range.opForEnd(iterator);
     iterator = range.opForNext(iterator)) {
    auto value = range.opForValue0(iterator);
    // body
}
```

`continue` targets the `opForNext` call, while `break` exits without advancing.
No foreach-specific opcode is needed; ordinary local, jump, `Call`,
`CallVirtual`, and `CallHost` instructions preserve the teaching-oriented VM.

## Verification

Tests cover keyword and AST shape, inferred and explicit item types, single and
numbered value operators, registered arrays, script-defined ranges, exact
protocol diagnostics, break/continue targets, runtime exception locations,
emitted call opcodes, and bytecode round trips retaining foreach syntax trees.

The differential case executes the same one- and two-item array loops with the
official AngelScript 2.38.0 scriptarray add-on.
