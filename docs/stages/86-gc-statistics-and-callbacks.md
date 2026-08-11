# Stage 86: GC statistics and circular-reference callbacks

## Goal

Hosts can monitor garbage-collector pressure and inspect objects when a cycle is
detected:

```cpp
auto stats = engine->GetGarbageCollectionStatistics();
engine->SetCircularReferenceDetectedCallback(
    [](const TypeInfo* type, const RefObject* object) { /* inspect only */ });
```

Statistics mirror the AngelScript 2.38.0 categories:

- `currentSize`: objects currently tracked by the collector;
- `totalDestroyed`: all tracked objects destroyed since engine creation;
- `totalDetected`: objects classified as circular garbage;
- `newObjects`: tracked objects not yet admitted to a detection cycle;
- `totalNewDestroyed`: new objects destroyed by reference counting before
  cycle detection.

Counters are monotonic for the engine lifetime, except the two current-size
gauges. Incremental detection moves the current new-object set into the active
snapshot when a cycle begins.

## Callback contract

The callback runs once for every object classified as circular garbage, after
classification and before references are cleared. It receives read-only type
and object pointers so a debugger can inspect concrete script fields or host
state. As in official AngelScript, modifying or retaining the reported objects
from this callback is unsupported because collection has already committed to
the detected set.

The `mini_as::compat` facade exposes the official-style GC flags,
`GarbageCollect`, `GetGCStatistics`, and `SetCircularRefDetectedCallback` while
forwarding to the native incremental collector. Integer counters saturate when
narrowed to the official 32-bit surface.

Tests distinguish ordinary reference-count destruction from detected cycles,
verify every counter transition, inspect objects before clearing, and exercise
both one-step and full-cycle compatibility calls.
