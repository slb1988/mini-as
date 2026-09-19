# feat(language): support auto declarations

- Commit: ae67831506a8a9f0f9a964c6d9893908d3145b75
- Date: 2026-08-08 12:18:35 +0800
- Author: sunlaibing

## 变更内容

支持 `auto` 声明：initializer 表达式完成类型检查后从中推断类型。

- 逗号分隔声明中，第一个 initializer 决定共享的声明类型，与 AngelScript 的声明规则一致；每个名字在下一个 initializer 检查前即可见
- initializer 必填（没有可回退的声明类型）；推断出的类型写回普通 `VarDecl` 节点，bytecode 生成与数值转换走与显式声明相同的路径
- `const auto` 把类型推断与 stage 17 的只读保护组合起来；产生对象的表达式保留其推断出的 handle 类型

## 相关文档

- [docs/stages/18-auto-declarations.md](../stages/18-auto-declarations.md)

## 涉及文件

```
 CMakeLists.txt                          |  7 +++++++
 docs/stages/18-auto-declarations.md     | 13 +++++++++++++
 include/mini_as/parser.hpp              |  1 +
 include/mini_as/tokenizer.hpp           |  2 +-
 src/parser.cpp                          |  9 ++++++---
 src/tokenizer.cpp                       |  5 +++--
 src/type_checker.cpp                    | 13 +++++++++++--
 tests/compat/cases/auto_declarations.as |  7 +++++++
 tests/test_engine.cpp                   | 27 +++++++++++++++++++++++++++
 tests/test_parser.cpp                   | 12 ++++++++++++
 10 files changed, 88 insertions(+), 8 deletions(-)
```
