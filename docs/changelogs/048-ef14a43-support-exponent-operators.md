# feat(expressions): support exponent operators

- Commit: ef14a432574623db7baac14d1b239b1404985d80
- Date: 2026-08-08 14:07:03 +0800
- Author: sunlaibing

## 变更内容

支持指数运算符 `**` 与 `**=`：

- `**` 优先级高于乘法，遵循 AngelScript 2.38.0 的左结合；`**=` 走共享 lvalue 管线，local/global/field 只求值一次，运算后窄化回声明类型
- 带类型的 `POW_I`/`POW_F`/`POW_D` 指令保持常规数值提升结果；整数幂使用平方求幂（exponentiation by squaring），非零底数配负指数返回 0，溢出与 `0 ** 0` 定义域错误抛出带位置的 `exponent overflow` 异常；浮点幂用标准库 `pow`
- 常量求值器折叠同一运算符，供 switch case 等编译期消费者使用；非数值操作数是编译诊断

## 相关文档

- [docs/stages/36-exponent-operators.md](../stages/36-exponent-operators.md)

## 涉及文件

```
 CMakeLists.txt                           |  7 +++++++
 docs/stages/36-exponent-operators.md     | 15 +++++++++++++++
 include/mini_as/bytecode.hpp             |  6 +++---
 include/mini_as/parser.hpp               |  1 +
 include/mini_as/tokenizer.hpp            |  4 ++--
 src/bytecode.cpp                         |  9 +++++++--
 src/constant_evaluator.cpp               | 17 +++++++++++++++++
 src/parser.cpp                           |  6 ++++--
 src/tokenizer.cpp                        |  9 +++++----
 src/vm.cpp                               | 84 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++----
 tests/compat/cases/exponent_operators.as |  9 ++++++++
 tests/test_bytecode.cpp                  | 16 ++++++++++++++
 tests/test_constant_evaluator.cpp        |  1 +
 tests/test_engine.cpp                    | 30 +++++++++++++++++++++++++++
 tests/test_tokenizer.cpp                 |  9 ++++++++
 15 files changed, 206 insertions(+), 17 deletions(-)
```
