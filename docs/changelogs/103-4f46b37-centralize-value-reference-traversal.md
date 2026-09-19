# refactor(runtime): centralize value reference traversal

- Commit: 4f46b37a18ae96bf62142df7b4a4b674189a3b03
- Date: 2026-08-11 22:10:40 +0800
- Author: sunlaibing

## 变更内容

把 GC 引用遍历集中到 `Value` 层：

- 新增 `ReferenceVisitor` 类型；`HostValueStorage` 增加可选的 `enumerateReferences`/`clearReferences` 回调；`Value::ManagedHostValue` 工厂让持有强对象引用的注册值类型接入 GC 遍历
- `ScriptObject`、script_array、script_dictionary 的枚举/清理逻辑改为委托给 `Value`，不再各自检查 variant 并假设「对象类型的值一定是 `ObjectHandle`」
- AGENTS.md 补充相应护栏：容器与脚本字段把 GC 遍历委托给 `Value`；持有强引用的注册值类型必须用 `ManagedHostValue`

为后续 any/ref add-on（依赖该机制）铺路；附 GC 遍历测试。

## 涉及文件

```
 AGENTS.md                 |  4 ++++
 include/mini_as/core.hpp  | 30 +++++++++++++++++++++++-
 src/core.cpp              | 59 +++++++++++++++++++++++++++++++++++++++++++++++
 src/object.cpp            | 34 ++-------------------------
 src/script_array.cpp      | 20 +++-------------
 src/script_dictionary.cpp | 22 ++++--------------
 tests/test_gc.cpp         | 34 +++++++++++++++++++++++++++
 7 files changed, 136 insertions(+), 67 deletions(-)
```
