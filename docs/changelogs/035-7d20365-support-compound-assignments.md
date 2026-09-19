# feat(expressions): support compound assignments

- Commit: 7d203659f8d73f63a436f7b7e515906d18bd45f5
- Date: 2026-08-08 12:42:38 +0800
- Author: sunlaibing

## 变更内容

支持复合赋值运算符：

- 数值 lvalue 支持 `+=`、`-=`、`*=`、`/=`、`%=`；string lvalue 通过既有拼接转换规则支持 `+=`；类型检查器先验证运算结果再回写，并保留 const 保护
- 编译走统一的 `LValueRef`（stage 024）：local/global 经稳定 ID 加载-运算-存储；field 赋值先只求值一次 receiver，在 operand stack 上保留副本，再加载旧字段值、运算、存回
- 表达式整体仍求值为被赋予的值；算术运行时失败保留复合赋值表达式的源码位置

## 相关文档

- [docs/stages/25-compound-assignments.md](../stages/25-compound-assignments.md)

## 涉及文件

```
 CMakeLists.txt                             |  7 +++++
 docs/stages/25-compound-assignments.md     | 13 +++++++++
 include/mini_as/bytecode.hpp               |  2 ++
 include/mini_as/tokenizer.hpp              |  1 +
 src/bytecode.cpp                           | 45 +++++++++++++++++++++++++++++-
 src/parser.cpp                             |  3 +-
 src/tokenizer.cpp                          | 11 ++++----
 src/type_checker.cpp                       | 21 +++++++++++++-
 tests/compat/cases/compound_assignments.as |  9 ++++++
 tests/test_engine.cpp                      | 31 ++++++++++++++++++++
 10 files changed, 135 insertions(+), 8 deletions(-)
```
