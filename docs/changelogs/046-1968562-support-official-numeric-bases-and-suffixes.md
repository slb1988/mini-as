# feat(literals): support official numeric bases and suffixes

- Commit: 19685627fd31d04d3dae8758bdcb09cc77257cf6
- Date: 2026-08-08 13:52:01 +0800
- Author: sunlaibing

## 变更内容

数值字面量对齐 AngelScript 官方规则：

- scanner 识别 `0b`/`0o`/`0d`/`0x` 进制整数、十进制指数、前导/尾随小数点与 `f` 后缀；无后缀实数字面量为 `double`，带 `f` 后缀为 `float`
- 字面量类型选择规则：十进制整数按 `int`（至 INT32_MAX）→ `int64`（至 INT64_MAX）→ `uint64` 递进；进制字面量 32 位内选 `uint`，否则 `uint64`；超过 64 位或前缀后无数字属于编译诊断而非静默截断
- 新增共享的 `DecodeNumericLiteral`，被类型检查、常量求值、bytecode 常量与 M0 解释器共同使用——一个字面量的类型与精确位模式在所有执行路径中保持一致；算术仍走 stage 32/33 的显式提升与转换规则

同时更新了 stage 33 文档中关于字面量行为的描述。

## 相关文档

- [docs/stages/34-numeric-literals.md](../stages/34-numeric-literals.md)
- [docs/stages/33-double-precision.md](../stages/33-double-precision.md)

## 涉及文件

```
 CMakeLists.txt                         |   7 +++++
 docs/stages/33-double-precision.md     |   7 +++--
 docs/stages/34-numeric-literals.md     |  17 +++++++++++
 include/mini_as/constant_evaluator.hpp |   2 ++
 include/mini_as/tokenizer.hpp          |   2 +-
 src/constant_evaluator.cpp             | 120 ++++++++++++++++++++++++++++++++++++++++++++-------
 src/interpreter.cpp                    |  10 ++++-
 src/parser.cpp                         |   3 +-
 src/tokenizer.cpp                      |  50 ++++++++++++++++++++----
 src/type_checker.cpp                   |   9 ++++-
 tests/compat/cases/numeric_literals.as |  15 ++++++++
 tests/test_bytecode.cpp                |   2 +-
 tests/test_constant_evaluator.cpp      |  19 ++++++++++
 tests/test_engine.cpp                  |  27 +++++++++++++
 tests/test_tokenizer.cpp               |  17 +++++++++
 15 files changed, 273 insertions(+), 34 deletions(-)
```
