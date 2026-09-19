# refactor(runtime): unify script host method and virtual call descriptors

- Commit: ba2f9a38bee323d0e7fe226c1c452574255c72bf
- Date: 2026-08-08 12:10:10 +0800
- Author: sunlaibing

## 变更内容

统一调用描述符：新增 `CallableRef`（`CallableKind` 区分 ScriptFunction / HostFunction / ScriptMethod / HostMethod / VirtualMethod，携带 `FunctionId`、`TypeId` 与虚函数槽位），`BytecodeModule` 集中存放 `callables` 表并支持按索引查询。

`CallHost` 等指令的操作数从直接的 host id 改为 callable 表索引，VM 按描述符分派——为实例方法、虚函数分派等后续阶段（28、31 等）建立统一的运行时表示。

## 涉及文件

```
 include/mini_as/bytecode.hpp | 18 ++++++++++++++++++
 src/bytecode.cpp             | 19 +++++++++++++++++--
 src/vm.cpp                   | 14 ++++++++++----
 tests/test_bytecode.cpp      |  5 ++++-
 4 files changed, 49 insertions(+), 7 deletions(-)
```
