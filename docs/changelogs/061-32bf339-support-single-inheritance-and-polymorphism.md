# feat(classes): support single inheritance and polymorphism

- Commit: 32bf3391bb393d92ea663a5effc30787aa245861
- Date: 2026-08-09 11:12:16 +0800
- Author: sunlaibing

## 变更内容

script class 支持单继承与多态（如 `class Derived : Base, IValue`）：

- 类型检查器决议前向声明的继承层级，拒绝环与多个具体基类；把继承字段平铺在派生字段之前；校验 override 签名；允许派生类 handle 到任意基类的隐式转换
- 所有普通 script 方法都参与虚分派（匹配 AngelScript 默认方法语义）：每个类获得稳定虚布局——继承槽位保持位置、override 原地替换实现、新重载追加槽位；`CallVirtual` 用 receiver 的静态类 + slot，VM 按对象真实 `TypeId` 选择实现；`Base::method()` 限定调用绕过虚槽直接调用基类实现
- 派生构造器可调用一次 `super(...)`；省略时编译器注入最近可用的默认基类构造器；无自有构造器的类在分配点获得同样的隐式链；基类字段槽位在前，字段 initializer 按基→派生顺序执行；本教学实现刻意不建模分支中 `super()` 的全部路径敏感规则
- 析构沿用 stage 44 的引擎队列：派生对象的 finalizer 绑定保存完整析构链，按派生→基顺序执行；某个析构失败只诊断上报，不影响其余基类析构运行
- 本阶段不含显式 downcast、`final`、`abstract`、`override` 与成员访问控制（留给后续阶段）

## 相关文档

- [docs/stages/45-single-inheritance.md](../stages/45-single-inheritance.md)

## 涉及文件

```
 AGENTS.md                                |   6 +++---
 CMakeLists.txt                           |   7 +++++++
 docs/stages/45-single-inheritance.md     |  31 +++++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp             |   7 +++++++
 include/mini_as/engine.hpp               |   1 +
 include/mini_as/object.hpp               |   7 +++++--
 include/mini_as/parser.hpp               |   2 ++
 include/mini_as/type_checker.hpp         |  12 ++++++++++++
 src/bytecode.cpp                         | 258 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-----------
 src/engine.cpp                           |  50 +++++++++++++++++++++++++---------------------
 src/object.cpp                           |  13 ++++++++++---
 src/parser.cpp                           |   8 ++++++--
 src/type_checker.cpp                     | 216 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-----
 src/vm.cpp                               |   4 +++-
 tests/compat/cases/single_inheritance.as |  15 +++++++++++++++
 tests/test_bytecode.cpp                  |  37 +++++++++++++++++++++++++++++++++++++
 tests/test_engine.cpp                    | 100 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                    |  15 +++++++++++++++
 18 files changed, 725 insertions(+), 64 deletions(-)
```
