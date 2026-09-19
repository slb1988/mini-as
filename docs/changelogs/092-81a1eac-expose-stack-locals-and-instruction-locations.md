# feat(debug): expose stack locals and instruction locations

- Commit: 81a1eacd1468a096801493f591b77cb63cba9c40
- Date: 2026-08-11 13:34:15 +0800
- Author: sunlaibing

## 变更内容

让 VM 既有的源码位置对内嵌调试器可用（对齐 2.38.0 `asIScriptContext` 调试表面的语义）：

- `ScriptContext` 新增 `GetCallStackSize()`、`GetFunction(stackLevel)`（0 为当前函数）、`GetInstructionLocation(stackLevel)`（section/row/column/offset）、`GetLocals(stackLevel)`（返回拷贝的 `LocalVariableInfo`：源码名、声明类型、稳定槽位、const/参数标记、当前值、`inScope`）；在 line callback 中、suspend 时、未处理异常后均可检查；返回 `Value` 拷贝避免暴露执行恢复后即失效的 VM 栈地址
- `BytecodeFunction::debugVariables` 只记录具名源码变量，编译器生成的 selector/receiver 槽位保持隐藏；调试作用域与词法符号作用域同开同闭；参数与隐式 `this` 覆盖全函数
- VM 检查闭包捕获时解开 cell，调试器看到的是当前捕获值；未处理异常时 `Fail` 在释放执行存储前快照每一帧
- 局部调试表进入 bytecode 归档，格式升至 version 2，加载时校验槽位与作用域范围
- 既有面向异常的 `GetCallStack()` 保持源码兼容，其 `StackFrameInfo` 现在也保留函数、指令偏移与局部快照

## 相关文档

- [docs/stages/71-debug-stack-locals.md](../stages/71-debug-stack-locals.md)

## 涉及文件

```
 AGENTS.md                                 |  13 ++-
 CMakeLists.txt                            |   7 ++
 docs/stages/71-debug-stack-locals.md      |  65 ++++++++++++++++
 include/mini_as/bytecode.hpp              |  18 ++++-
 include/mini_as/engine.hpp                |   4 +
 include/mini_as/vm.hpp                    |  19 +++++
 src/bytecode.cpp                          |  58 ++++++++++++--
 src/bytecode_io.cpp                       |  41 +++++++++-
 src/engine.cpp                            |  27 +++++++
 src/vm.cpp                                |  83 ++++++++++++++++++--
 tests/compat/cases/debug_introspection.as |   9 +++
 tests/compat/mini_runner.cpp              |  26 +++++++
 tests/compat/official_runner.cpp          |  47 +++++++++++
 tests/test_bytecode.cpp                   |  32 ++++++++
 tests/test_engine.cpp                     | 131 +++++++++++++++++++++++++++++++
 15 files changed, 560 insertions(+), 20 deletions(-)
```
