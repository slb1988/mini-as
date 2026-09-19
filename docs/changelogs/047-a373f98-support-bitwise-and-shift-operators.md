# feat(expressions): support bitwise and shift operators

- Commit: a373f98eac17f0aa7c3761801a1c11f87d4613c4
- Date: 2026-08-08 13:59:49 +0800
- Author: sunlaibing

## 变更内容

表达式文法新增位运算与移位：`~`、`&`、`|`、`^`、`<<`、`>>`、`>>>`（各有独立 precedence level），以及全部六种复合赋值形式。

- 当前原始类型模型下只接受整数操作数；结果保持左操作数的声明类型与宽度
- 专用 bytecode 指令直接操作整数位；`>>` 遵循 AngelScript 的逻辑右移，`>>>` 执行官方 2.38.0 编译器使用的算术右移；移位计数按操作数宽度归一化，结果经 `Value::Integer` 截断，保留窄整数回绕语义
- 常量表达式求值器实现相同运算，因此位运算常量可用于 switch case；浮点操作数在生成 bytecode 前被诊断

## 相关文档

- [docs/stages/35-bitwise-shifts.md](../stages/35-bitwise-shifts.md)

## 涉及文件

```
 CMakeLists.txt                       |  7 +++++++
 docs/stages/35-bitwise-shifts.md     | 16 ++++++++++++++++
 include/mini_as/bytecode.hpp         |  3 ++-
 include/mini_as/parser.hpp           |  4 ++++
 include/mini_as/tokenizer.hpp        |  2 ++
 src/bytecode.cpp                     | 29 +++++++++++++++++++++++++----
 src/constant_evaluator.cpp           | 15 +++++++++++++++
 src/parser.cpp                       | 15 +++++++++++----
 src/tokenizer.cpp                    | 21 +++++++++++++++++----
 src/type_checker.cpp                 | 22 +++++++++++++++++++++-
 src/vm.cpp                           | 27 +++++++++++++++++++++++++++
 tests/compat/cases/bitwise_shifts.as | 15 +++++++++++++++
 tests/test_bytecode.cpp              | 18 ++++++++++++++++++
 tests/test_engine.cpp                | 23 +++++++++++++++++++++++
 tests/test_tokenizer.cpp             | 21 +++++++++++++++++++++
 15 files changed, 224 insertions(+), 14 deletions(-)
```
