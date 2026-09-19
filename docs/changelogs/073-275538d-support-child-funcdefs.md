# feat(functions): support child funcdefs

- Commit: 275538d06852b2fa62381b0090881f3b61927253
- Date: 2026-08-09 19:04:17 +0800
- Author: sunlaibing

## 变更内容

funcdef 可声明为 script class 的成员（child funcdef）：

- 全限定类型名为 `Parent::Name`；父类内部可用短名，派生类作用域中短名同样可见，其他位置用限定名（与 2.38.0 一致）
- parser 的声明预扫描在解析函数体前记录类持有的 funcdef 名与基类关系，类型查找与源码顺序无关（包括先于父类出现的限定使用）；child 声明是类 AST 内专门的 `FuncdefDecl`，不会被误认为字段或无体方法
- `FuncdefSignature` 新增 `parentType`；两个类可声明同名同形的 child funcdef，但 handle 类型因限定名与 ID 不同而保持不同身份
- 无需新调用 opcode：child handle 复用既有函数地址、字段/全局/局部存储、`CallHandle` 与 null 异常路径
- 按官方文法，interface 中的 child funcdef 被诊断；同一父类内重复声明、跨父类 handle 赋值、默认参数沿用普通 funcdef 诊断

## 相关文档

- [docs/stages/55-child-funcdefs.md](../stages/55-child-funcdefs.md)

## 涉及文件

```
 AGENTS.md                            |   4 ++--
 CMakeLists.txt                       |   7 +++++++
 docs/stages/55-child-funcdefs.md     |  43 +++++++++++++++++++++++++++++++++++++++++++
 include/mini_as/parser.hpp           |   4 +++-
 include/mini_as/type_checker.hpp     |   1 +
 src/parser.cpp                       | 100 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++---
 src/type_checker.cpp                 |  37 ++++++++++++++++++++++++++++++++-----
 tests/compat/cases/child_funcdefs.as |  25 +++++++++++++++++++++++++
 tests/test_engine.cpp                |  83 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                |  33 +++++++++++++++++++++++++++++++++
 tests/test_types.cpp                 |  18 ++++++++++++++++++
 11 files changed, 341 insertions(+), 14 deletions(-)
```
