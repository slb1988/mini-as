# feat(language): support switch case and default

- Commit: 3b68dc50a6638c7d69fcfd15bdac5624aa12eaa5
- Date: 2026-08-08 12:33:52 +0800
- Author: sunlaibing

## 变更内容

支持 `switch` 语句：

- 接受整数 selector、整数常量表达式 `case` 标签，至多一个 `default`；类型检查器用共享的常量表达式求值器（stage 023 抽出的 `ConstantExpressionEvaluator`）计算标签值，并在生成 bytecode 前拒绝重复值
- bytecode 只保存一次 selector，发射比较分派序列，然后按源码顺序排布各子句体；子句体之间不插入隐式跳转，保留 AngelScript 的 fall-through 行为
- 无 case 匹配时跳到 `default`，无 default 则越过整个 switch

新增差分用例 `switch_fallthrough.as`。

## 相关文档

- [docs/stages/22-switch-statements.md](../stages/22-switch-statements.md)

## 涉及文件

```
 CMakeLists.txt                           |  7 ++++++
 docs/stages/22-switch-statements.md      | 11 +++++++++
 include/mini_as/parser.hpp               |  4 +++-
 include/mini_as/tokenizer.hpp            |  3 ++-
 src/bytecode.cpp                         | 41 ++++++++++++++++++++++++++++++++
 src/parser.cpp                           | 36 +++++++++++++++++++++++++++-
 src/tokenizer.cpp                        |  6 +++--
 src/type_checker.cpp                     | 33 ++++++++++++++++++++++++-
 tests/compat/cases/switch_fallthrough.as | 10 ++++++++
 tests/test_engine.cpp                    | 35 +++++++++++++++++++++++++++
 tests/test_parser.cpp                    | 17 +++++++++++++
 11 files changed, 197 insertions(+), 6 deletions(-)
```
