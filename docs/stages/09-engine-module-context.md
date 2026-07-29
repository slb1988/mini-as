# 09 - Engine, module, and context

The public API now separates three lifetimes. `ScriptEngine` owns registered
configuration and named modules. `ScriptModule` collects source sections and
atomically replaces its bytecode only after a successful build. `ScriptContext`
owns mutable execution state and may be created many times for the same
immutable function.

Multiple source sections become one token stream rather than one concatenated
string, preserving each token's section name in diagnostics. The context's
`Prepare, SetArg, Execute, GetReturn` sequence mirrors AngelScript while RAII
replaces public reference-counting calls in this educational API.

Tests exercise the complete public pipeline, cross-section compilation,
module creation policy, typed argument validation, and forwarded diagnostics.
