# Stage 87: context pooling

## Goal

Allow hosts and later runtime add-ons to reuse script contexts without changing
the ownership or behavior of the existing `CreateContext()` convenience API.
The design follows AngelScript 2.38.0's `RequestContext`, `ReturnContext`, and
paired context-callback model while retaining ordinary C++ ownership tools.

## Public surface

- `ScriptEngine::RequestContext()` obtains a raw context from the configured
  request callback, or allocates one through `CreateContext()` by default.
- `ScriptEngine::ReturnContext()` sends it to the paired return callback, or
  deletes it when no pool is configured.
- `ScriptEngine::SetContextCallbacks()` accepts either both callbacks or neither;
  a half-configured pool is rejected.
- `ScriptContext::Unprepare()` releases execution stacks, arguments, return
  values, and the retained immutable module image, then restores the
  `Uninitialized` state.
- `mini_as::compat` exposes the same flow with official-style result codes.

The request/return pair intentionally uses raw pointers because ownership is in
transit, matching the official API. Pool implementations should immediately put
returned pointers back under `std::unique_ptr` ownership.

## Lifecycle rules

`Unprepare()` is rejected while a context is active or suspended. A scheduler
must finish or abort a suspended context before returning it to a general pool.
The line callback is context configuration and survives unprepare; execution
state and module ownership do not. Reconfiguring or clearing callbacks affects
future requests and returns only, so all outstanding contexts must still be
returned to the origin that issued them.

## Verification

The native and compatibility-facade tests cover callback-pair validation,
allocation fallback, pointer reuse, state cleanup, execution after reuse, and
rejection of unprepare during active or suspended execution. This host-side
runtime policy has no script-semantic differential case.
