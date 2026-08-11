# Stage 88: cooperative coroutines

## Goal

Provide a small host-driven coroutine scheduler on top of the existing typed VM
pause/resume machinery and Stage 87 context pool. The model follows the
AngelScript 2.38.0 `contextmgr` add-on: scripts yield voluntarily, and the host
advances every ready context once per application tick.

## Runtime surface

`CoroutineScheduler` owns contexts requested from one `ScriptEngine` and
provides:

- `RegisterYieldFunction()` for the script-visible `void yield()` helper;
- `Start()` for global script functions with typed initial arguments;
- `ExecuteRound()` for fair, one-slice-per-ready-coroutine scheduling;
- `Yield()`, `Abort()`, and `AbortAll()` for host-side control;
- stable `CoroutineId` values and retained `CoroutineResult` records.

A result copies the return value or exception text, source location, and stack
frames before the context is returned to its engine. `TakeCompleted()` transfers
one result to the host and removes it from the completion queue.

## Suspension and fairness

The compiler already emits a `Suspend` cue at each statement boundary. Calling
the registered `yield()` requests suspension, so the current statement finishes
and the VM returns at the next cue. `ExecuteRound()` snapshots the number of
ready tasks at entry; coroutines created by host callbacks wait until the next
round, preventing one producer from starving existing work.

Every suspended task is appended to the ready queue. A task that finishes,
aborts, or throws is removed and its context is sent through
`ScriptEngine::ReturnContext()`, making application context pools available to
the scheduler without duplicate ownership rules.

## Safe cancellation

Cancelling the currently executing coroutine cannot clear VM stacks from inside
a host callback. The scheduler records the request, asks the VM to suspend, and
performs `Abort()` only after `Execute()` has returned. Queued contexts can be
aborted immediately. `yield()` called from a normal context, or after its
scheduler has been destroyed, becomes a located runtime exception instead of
dereferencing stale scheduler state.

The scheduler is intentionally single-threaded, like the official add-on. It
must be destroyed before its engine because it returns all outstanding contexts
during destruction.

## Verification

Tests cover round-robin ordering, bytecode suspension cues, return values,
yield-then-exception source locations, invalid start arguments, queued and
self-cancellation, safe callback lifetime, and context-pool reuse. Coroutine
scheduling is a host add-on policy, so there is no script-only differential
case in the official corpus.
