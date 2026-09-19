# feat(addon): provide any and ref objects

- Commit: 6581c12098762b1588ae73ef88c6dd4d6f4f086e
- Date: 2026-08-11 22:15:41 +0800
- Author: sunlaibing

## 变更内容

add-on 库新增两个小型类型擦除工具：

- **`any`**：可被 GC 的引用对象，内含一个拷贝的 `Value`；注册 factory 与 `store`/`retrieve` 重载（`int64`/`double`/内置 `string`/`bool`，以及 ref add-on 已注册时的 `ref`）；另有 `hasValue`/`typeName`/`clear`；数值 retrieve 执行与其他宿主面值相同的受检转换，类型不匹配返回 `false` 且不改 out 参数
- **`ref`**：注册值类型，内含任意对象 handle；注册默认/拷贝值语义、`opEquals`、`isNull`、`typeName`；嵌入方可用 `MakeScriptRef`/`GetScriptRef` 桥接任意已注册对象 handle；官方通配符构造与泛型 cast 依赖通配符参数，刻意留给变参阶段
- 两者保持为 add-on 而非 VM 原语，编译为既有 factory 与宿主方法调用指令；同时使用两个 add-on 时须先注册 `ref`
- GC：复用上一阶段集中的 `Value` 引用遍历——`ScriptAny` 枚举并清理其负载引用；ref 值通过 `Value::ManagedHostValue` 回调参与循环检测，即使它由宿主值存储而非直接 `ObjectHandle` 表示

## 相关文档

- [docs/stages/80-any-ref-objects.md](../stages/80-any-ref-objects.md)

## 涉及文件

```
 AGENTS.md                             |   8 +-
 CMakeLists.txt                        |  13 ++++
 docs/stages/80-any-ref-objects.md     |  63 +++++++++++++++
 include/mini_as/addons/any.hpp        |  29 +++++++
 include/mini_as/addons/ref.hpp        |  15 ++++
 src/script_any.cpp                    | 138 ++++++++++++++++++++++++++++++++++
 src/script_ref.cpp                    |  40 ++++++++++
 tests/compat/cases/any_ref_objects.as |  12 +++
 tests/compat/mini_runner.cpp          |   6 ++
 tests/compat/official_runner.cpp      |   6 ++
 tests/test_addons.cpp                 | 141 ++++++++++++++++++++++++++++++++++
 11 files changed, 469 insertions(+), 2 deletions(-)
```
