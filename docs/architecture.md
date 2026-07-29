# Architecture

```text
source sections
  -> Tokenizer             tokens with section/row/column
  -> Parser                arena-owned sibling AST
  -> TypeChecker           scopes, signatures, typed expressions
  -> BytecodeCompiler      constants, slots, jumps, line cues
  -> ScriptModule          immutable functions and type metadata
  -> ScriptContext / VM    operand stack, locals, call frames, pc
       -> CALL_HOST        GenericCall into registered C++
       -> ScriptObject     intrusive handles and indexed fields
       -> GarbageCollector trial-deletes unreachable cycles
```

The engine is statically typed but stores transparent tagged runtime values.
The VM stack and call frames are heap-owned, so suspension never needs to save
a native C++ stack. Engine configuration, compiled modules, and mutable contexts
have separate ownership exactly because they change at different rates.
