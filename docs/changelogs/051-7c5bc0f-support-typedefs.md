# feat(language): support typedefs

- Commit: 7c5bc0fde032ccbf276318051fcdce4961337414
- Date: 2026-08-09 08:23:23 +0800
- Author: sunlaibing

## 变更内容

支持 `typedef <primitive> <name>;`：为内置原始类型引入 module 级别名，与 AngelScript 2.38.0 对 script typedef 的限制一致。

- 别名可用于全局、局部、字段、参数与返回类型——包括在合并后的 module 中出现位置早于 typedef 的声明
- parser 先发现别名再构建声明节点，并把别名决议为其规范存储类型；类型检查器保留带稳定 `TypeId` 的独立 `TypedefSignature` 供后续反射使用，而当前 bytecode 与 `Value` 表示直接使用底层原始类型
- 别名重名与源类型非原始类型均为编译诊断

## 相关文档

- [docs/stages/38-typedefs.md](../stages/38-typedefs.md)

## 涉及文件

```
 CMakeLists.txt                   |  7 +++++++
 docs/stages/38-typedefs.md       | 13 +++++++++++++
 include/mini_as/parser.hpp       |  5 ++++-
 include/mini_as/tokenizer.hpp    |  2 +-
 include/mini_as/type_checker.hpp |  9 +++++++++
 src/engine.cpp                   |  2 ++
 src/parser.cpp                   | 47 ++++++++++++++++++++++++++++++++++++++++++++----
 src/tokenizer.cpp                |  5 +++--
 src/type_checker.cpp             | 17 +++++++++++++++++
 tests/compat/cases/typedefs.as   | 12 ++++++++++++
 tests/test_engine.cpp            | 30 ++++++++++++++++++++++++++++++
 tests/test_parser.cpp            | 26 ++++++++++++++++++++++++++
 tests/test_tokenizer.cpp         | 10 ++++++++++
 tests/test_types.cpp             | 15 +++++++++++++++
 14 files changed, 193 insertions(+), 7 deletions(-)
```
