# feat(expressions): support prefix and postfix increment

- Commit: c8ea7e6550e3bb7f6b8a559fc77883f7dada2a66
- Date: 2026-08-08 12:45:37 +0800
- Author: sunlaibing

## 变更内容

支持前/后缀 `++` 与 `--`：

- 适用于数值 local、global 与 field lvalue；const 与非 lvalue 操作数在类型检查期拒绝
- 前缀形式存回并返回新值；后缀形式在更新序列之下保留旧值，存储后丢弃新值——即「先取旧值再自增」语义
- field 更新只求值一次 receiver；新增小型栈操作 `SWAP` 来安排 receiver 与保留值，避免引入用户可见的临时 local

## 相关文档

- [docs/stages/26-increment-expressions.md](../stages/26-increment-expressions.md)

## 涉及文件

```
 CMakeLists.txt                              |  7 +++++++
 docs/stages/26-increment-expressions.md     |  9 +++++++++
 include/mini_as/bytecode.hpp                |  3 ++-
 include/mini_as/parser.hpp                  |  3 ++-
 include/mini_as/tokenizer.hpp               |  1 +
 src/bytecode.cpp                            | 44 ++++++++++++++++++++++++++++++++++++-
 src/parser.cpp                              | 10 +++++++++
 src/tokenizer.cpp                           |  9 ++++++--
 src/type_checker.cpp                        |  9 ++++++++
 src/vm.cpp                                  |  6 ++++++
 tests/compat/cases/increment_expressions.as |  9 ++++++++
 tests/test_engine.cpp                       | 33 +++++++++++++++++++++++++++++
 12 files changed, 138 insertions(+), 5 deletions(-)
```
