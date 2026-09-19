# feat(classes): support private and protected members

- Commit: b136a5468a54ca03e1ace01707c60b9ed0a3e7f0
- Date: 2026-08-09 11:24:01 +0800
- Author: sunlaibing

## 变更内容

script class 的字段、方法、构造器、析构器支持 `private`/`protected` 前缀；无限定时保持 public，与 AngelScript 2.38.0 一致；interface 仍只允许 public 方法并拒绝访问限定符。

- 类型模型记录每个成员的访问级别与声明类——对继承字段尤其重要：它们的物理槽位被平铺进派生对象布局，但归属不变
- `private` 成员只在编译其声明类的方法时可访问；`protected` 还可被派生类方法访问；全局函数与无关类两者都不可访问
- 访问检查发生在重载决议之后、bytecode 发射之前；构造器遵循同一规则——`protected` 基类构造器可经 `super(...)` 调用，`private` 不行；方法可见性不改变稳定虚槽位（分派仍按静态类 + slot + 真实 `TypeId`）
- 因为无效访问在 module 构建期已被拒绝，VM 不需要任何访问控制 opcode，运行时字段布局与调用分派保持紧凑不变
- 本阶段不含 reference cast、`final`、`abstract`、`override`

## 相关文档

- [docs/stages/46-member-access.md](../stages/46-member-access.md)

## 涉及文件

```
 AGENTS.md                           |  5 ++---
 CMakeLists.txt                      |  7 +++++++
 docs/stages/46-member-access.md     | 26 ++++++++++++++++++++++++++
 include/mini_as/parser.hpp          |  2 ++
 include/mini_as/tokenizer.hpp       |  1 +
 include/mini_as/type_checker.hpp    | 13 ++++++++++++-
 src/bytecode.cpp                    |  7 ++++---
 src/engine.cpp                      |  5 +++--
 src/parser.cpp                      | 15 +++++++++++++++
 src/tokenizer.cpp                   |  2 ++
 src/type_checker.cpp                | 74 ++++++++++++++++++++++++++++++++++++++++++++++++---------------------
 tests/compat/cases/member_access.as | 19 +++++++++++++++++++
 tests/test_bytecode.cpp             | 22 +++++++++++++++++++++-
 tests/test_engine.cpp               | 54 ++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp               | 27 +++++++++++++++++++++++++++
 tests/test_tokenizer.cpp            |  9 +++++++++
 16 files changed, 261 insertions(+), 27 deletions(-)
```
