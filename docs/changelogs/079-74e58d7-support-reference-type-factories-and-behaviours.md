# feat(host): support reference type factories and behaviours

- Commit: 74e58d71bd401c561845ca5c349ca21245bbbbf0
- Date: 2026-08-09 21:11:58 +0800
- Author: sunlaibing

## 变更内容

注册宿主引用类型可用原样的脚本语法构造（`HostRef@ value = HostRef(42);`），对应官方 2.38.0 的基本引用类型契约：可构造引用类型 = factory + addref + release 行为。

- factory 走 portable `GenericCall` 边界；addref/release 刻意做成内在行为——注册对象派生自 `RefObject`，每次 `ObjectHandle` 拷贝/移动/析构应用既有原子 `AddRef`/`Release`，handle 簿记中没有原生调用约定或用户回调
- factory 声明用官方 `Type@ f(...)` 拼写；重载按普通参数转换与命名实参规则选择，但 `f` 不发布为全局脚本函数；每个 factory 有稳定 `FunctionId` 与 factory 限定的持久键，与同声明的普通宿主函数不冲突
- 注册引用类型作为宿主 `ClassSignature` 预声明；类型检查器只对 factory 决议 `Type(args)`；编译器直接发射链接好的 `CallHost`，绝不发射 `NewObject`（宿主拥有分配与初始化）；无 factory 的注册类型刻意不可实例化（对应官方单例/对象池用法）
- VM 要求 factory 返回声明类型的非 null 对象：factory 可通过设置脚本异常失败；无异常却返回 null 会在构造位置被拒绝

## 相关文档

- [docs/stages/60-reference-type-factories.md](../stages/60-reference-type-factories.md)

## 涉及文件

```
 AGENTS.md                                      |  11 +++---
 CMakeLists.txt                                 |   7 +++++
 docs/stages/60-reference-type-factories.md     |  53 +++++++++++++++++++++++++++++
 include/mini_as/engine.hpp                     |   3 ++
 include/mini_as/object.hpp                     |   1 +
 include/mini_as/type_checker.hpp               |   4 +++
 src/bytecode.cpp                               |  44 ++++++++++++++++++++++---
 src/engine.cpp                                 |  69 ++++++++++++++++++++++++++++++++++----
 src/type_checker.cpp                           |  41 ++++++++++++++++++++---
 src/vm.cpp                                     |  11 +++++--
 tests/compat/cases/reference_type_factories.as |   4 +++
 tests/compat/mini_runner.cpp                   |  23 ++++++++++---
 tests/compat/official_runner.cpp               |  32 +++++++++++++++---
 tests/test_bytecode.cpp                        |  45 +++++++++++++++++++++++++
 tests/test_objects.cpp                         | 135 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 15 files changed, 460 insertions(+), 23 deletions(-)
```
