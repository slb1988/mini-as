# feat(types): support the signed and unsigned integer family

- Commit: 45203f09e57b22f175a7376b979ca67bd114f6bc
- Date: 2026-08-08 13:32:22 +0800
- Author: sunlaibing

## 变更内容

支持 AngelScript 的完整整数族：`int8`/`int16`/`int`(`int32`)/`int64`/`uint8`/`uint16`/`uint`(`uint32`)/`uint64`。

- `int` 仍是原 32 位 `Value` 变体，保持原嵌入 API 源码兼容；其他宽度使用带标签的整数负载（记录确切类型与归一化位模式）
- 整数转换是显式的 `TO_INTEGER` bytecode 操作；窄化用二进制补码截断，存储值与自增/复合赋值按声明宽度回绕（wrap）
- 算术先把 8/16 位操作数提升为 `int`，再取更宽类型；无符号操作数宽度不小于有符号时无符号胜出；整数 → `float` 仍是允许的加宽转换
- VM 的加减乘除、取负、取余与有序比较都不经过浮点：保证 64 位值精确，除零沿用带位置异常

数字进制前缀与字面量后缀刻意留给独立的 literals 阶段（034 的后一阶段）。

## 相关文档

- [docs/stages/32-integer-family.md](../stages/32-integer-family.md)

## 涉及文件

```
 CMakeLists.txt                       |  7 +++++++
 docs/stages/32-integer-family.md     | 20 ++++++++++++++++++++
 include/mini_as/bytecode.hpp         |  4 +++-
 include/mini_as/core.hpp             | 38 +++++++++++++++++++++++++++++++-
 include/mini_as/tokenizer.hpp        |  4 +++-
 include/mini_as/type_checker.hpp     |  1 +
 src/bytecode.cpp                     | 98 +++++++++++++++++++++++++++++++---------------------------
 src/core.cpp                         | 99 ++++++++++++++++++++++++++++++++++++++++++++++++++++++--
 src/engine.cpp                       |  8 ++++--
 src/generic.cpp                      |  7 +++++
 src/object.cpp                       |  4 +--
 src/parser.cpp                       | 11 ++++++--
 src/tokenizer.cpp                    |  7 +++--
 src/type_checker.cpp                 | 53 +++++++++++++++++++++----------------
 src/vm.cpp                           | 82 ++++++++++++++++++++++++++++++++++++++++----------
 tests/compat/cases/integer_family.as | 16 +++++++++++
 tests/test_bytecode.cpp              | 17 ++++++++++++
 tests/test_engine.cpp                | 32 ++++++++++++++++++++++
 tests/test_tokenizer.cpp             | 18 +++++++++++++
 tests/test_types.cpp                 | 12 +++++++++
 20 files changed, 454 insertions(+), 84 deletions(-)
```
