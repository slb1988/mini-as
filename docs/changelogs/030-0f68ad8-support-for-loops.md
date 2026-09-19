# feat(language): support for loops

- Commit: 0f68ad8f3bdeb048c0ddf94624f246d72c6b23e5
- Date: 2026-08-08 12:29:02 +0800
- Author: sunlaibing

## 变更内容

支持 `for` 循环：

- parser 把 `for` 建模为四个显式子节点：initializer、condition、increment、body；缺失的子句用 `EmptyStmt` 占位，避免后续编译阶段猜测子节点位置
- initializer 与循环体共享一个词法作用域；condition 存在时必须是 bool，省略时按 `true` 发射
- bytecode 从循环体跳经 increment 表达式再回到 condition，并丢弃 increment 表达式的值
- initializer 接受空、表达式、单声明与逗号分隔多声明四种形式

## 相关文档

- [docs/stages/20-for-loops.md](../stages/20-for-loops.md)

## 涉及文件

```
 CMakeLists.txt                  |  7 +++++++
 docs/stages/20-for-loops.md     | 13 +++++++++++++
 include/mini_as/parser.hpp      |  4 +++-
 include/mini_as/tokenizer.hpp   |  2 +-
 src/bytecode.cpp                | 19 +++++++++++++++++++
 src/parser.cpp                  | 41 +++++++++++++++++++++++++++++++++--------
 src/tokenizer.cpp               |  4 ++--
 src/type_checker.cpp            | 13 ++++++++++++-
 tests/compat/cases/for_loops.as |  5 +++++
 tests/test_engine.cpp           | 25 +++++++++++++++++++++++++
 tests/test_parser.cpp           | 16 ++++++++++++++++
 11 files changed, 136 insertions(+), 13 deletions(-)
```
