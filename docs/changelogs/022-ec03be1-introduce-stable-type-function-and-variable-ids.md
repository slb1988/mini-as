# refactor(symbols): introduce stable type function and variable ids

- Commit: ec03be12c23ec9fb4d6822658ad42b66313cb811
- Date: 2026-08-08 12:05:19 +0800
- Author: sunlaibing

## 变更内容

符号系统重构：新增 `symbols.hpp`，用稳定 ID（`FunctionId`/`TypeId`/`VariableId`，值为 `uint32_t` 的强类型包装）取代此前散落在各处的裸索引与指针表：

- `BytecodeModule` 改为持有 `FunctionId → RegisteredHostFunction*`、`TypeId → TypeInfo*` 的映射，并提供 `FindFunction/FindHostFunction/FindType` 查询
- 编译器内部的作用域表、函数表、host 表、类表全部改用 ID；engine 提供 `GetOrCreateFunctionId/TypeId` 保证同一声明键对应同一 ID

这是后续动态函数编译/移除、序列化等阶段的元数据地基。

## 涉及文件

```
 include/mini_as/bytecode.hpp     | 22 ++++++-----
 include/mini_as/engine.hpp       |  6 +++
 include/mini_as/object.hpp       |  2 +
 include/mini_as/symbols.hpp      | 27 +++++++++++++
 include/mini_as/type_checker.hpp |  3 ++
 include/mini_as/vm.hpp           |  7 +++-
 src/bytecode.cpp                 | 83 ++++++++++++++++++++++++++--------------
 src/engine.cpp                   | 40 +++++++++++++++----
 src/type_checker.cpp             |  4 +-
 src/vm.cpp                       | 36 ++++++++++-------
 tests/test_bytecode.cpp          | 23 +++++++++++
 tests/test_engine.cpp            | 16 ++++++++
 12 files changed, 207 insertions(+), 62 deletions(-)
```
