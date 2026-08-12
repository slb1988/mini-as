# Stage 85: incremental cycle detection

## Goal

Cycle detection can now be spread across ordinary application ticks instead of
scanning the entire object graph in one call:

```cpp
engine->CollectGarbageStep(8); // visit at most eight graph objects
```

The existing `CollectGarbage()` convenience API remains compatible and drives
the same state machine through a complete cycle.

## State machine

One collection cycle advances through four bounded phases:

1. count incoming references between tracked objects;
2. identify objects with an external reference as roots;
3. mark objects reachable from those roots;
4. select unmarked objects as cyclic garbage.

The budget counts visited graph objects. Destruction remains atomic after
classification: every garbage object receives a temporary hold before any
edge is cleared, then all holds are released. This preserves the reference
counting and finalizer invariants while keeping the more expensive detection
work incremental.

## Mutation safety

Tracked-object registration, destruction, `AddRef`, and `Release` advance a
collector generation. If the graph or external root set changes between two
steps, the partial classification is discarded and restarted from a new
snapshot. Acquiring a handle through a weak reference during an incremental
cycle therefore rescues the graph instead of exposing it to a stale decision.

`CollectGarbageStep(0)` is a no-op, and
`IsGarbageCollectionInProgress()` reports whether a partial cycle exists.

Tests cover bounded progress, zero-budget behavior, mutation-triggered restart,
weak-reference rescue, complete collection, finalizers, delegates, closures,
managed host values, and compatibility of the full-cycle API. This scheduler
detail has no script-visible differential case; AngelScript 2.38.0 remains the
behavioral reference for the one-step/full-cycle distinction.
