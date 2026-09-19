# feat(objects): support weak references

- Commit: 6676af2327713c0048dd438305b31628537e196c
- Date: 2026-08-09 19:23:37 +0800
- Author: sunlaibing

## 变更内容

支持官方 weakref add-on 形态的弱引用：`weakref<T>` 与 `const_weakref<T>`，适用于 script class 与 interface。

- 两者按专门的参数化值类型解析；本阶段以内在实现（intrinsic）方式落地，使弱引用先于 v0.6 的通用注册模板系统可用；脚本拼写、默认/显式构造、`get()`、隐式锁定、拷贝、相等性与显式 handle 赋值（`@reference = object`）与 2.38.0 add-on 表面对齐；模板参数必须是脚本引用类型且不含 `@`
- 每个 `RefObject` 持有共享 lifetime token；`WeakObjectHandle` 只存原始身份与 token，绝不持有 owning `ObjectHandle`；token 的 mutex 串行化「最后一次强 `Release`」与「弱锁定」：要么锁定先增加强计数，要么析构先把 token 标记为死——弱引用无法复活计数已归零的对象；token 还能区分已销毁对象与恰好复用同地址的新对象
- bytecode 只增三个小操作：`MakeWeakRef`（handle → 非持有值）、`LockWeakRef`（原子返回强 handle 或 null）、`ToConstWeakRef`（保留 token 并改弱引用类型）
- 弱值可用于 local/global/字段/参数/返回/捕获 cell；对象引用枚举忽略它们——弱自链不会成为 GC root；对象死亡后弱值仍是合法值并一致锁定为 null
- 边界：注册宿主类型的 weakref 行为等宿主注册路线图；`const_weakref` 保留弱生命周期与类型身份，但 mini 尚无 const 限定对象 handle，无法强制锁定对象的只读访问

## 相关文档

- [docs/stages/56-weak-references.md](../stages/56-weak-references.md)

## 涉及文件

```
 AGENTS.md                             |  4 +--
 CMakeLists.txt                        | 13 ++++++--
 docs/stages/56-weak-references.md     | 52 +++++++++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp          |  2 +-
 include/mini_as/core.hpp              | 36 +++++++++++++++++++++++-
 include/mini_as/object.hpp            |  2 ++
 src/bytecode.cpp                      | 42 +++++++++++++++++++++++++++-
 src/core.cpp                          | 14 +++++++++-
 src/engine.cpp                        |  2 ++
 src/object.cpp                        | 63 +++++++++++++++++++++++++++++++++++++++++--
 src/parser.cpp                        | 84 +++++++++++++++++++++++++++++++++++++++++++++++++++++-------
 src/type_checker.cpp                  | 70 +++++++++++++++++++++++++++++++++++++++++++++++-
 src/vm.cpp                            | 30 +++++++++++++++++++++
 tests/compat/cases/weak_references.as | 18 ++++++++++++
 tests/compat/official_runner.cpp      |  2 ++
 tests/test_engine.cpp                 | 56 ++++++++++++++++++++++++++++++++++++++
 tests/test_gc.cpp                     | 16 +++++++++++
 tests/test_objects.cpp                | 19 +++++++++++++
 tests/test_parser.cpp                 | 19 ++++++++++++++
 tests/test_types.cpp                  | 18 ++++++++++++
 20 files changed, 538 insertions(+), 24 deletions(-)
```
