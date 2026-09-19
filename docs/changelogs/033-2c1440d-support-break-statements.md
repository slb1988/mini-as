# feat(language): support break statements

- Commit: 2c1440d1fbdb57c6f06902c7b7944199fbe52344
- Date: 2026-08-08 12:36:42 +0800
- Author: sunlaibing

## 变更内容

支持 `break` 语句：

- 在 `while`、`do`/`while`、`for`、`switch` 内合法；类型检查器跟踪可 break 的嵌套层级，没有目标时报告编译错误
- bytecode 编译器维护一个控制流 context 栈：每个 `break` 向最近的 context 发出一个待解析跳转，该 context 把跳转回填到循环或 switch 之后的第一条指令；嵌套的 switch 与循环无需特判 AST 即可选中最近的正确目标

## 相关文档

- [docs/stages/23-break-statements.md](../stages/23-break-statements.md)

## 涉及文件

```
 CMakeLists.txt                         |  7 +++++++
 docs/stages/23-break-statements.md     | 10 ++++++++++
 include/mini_as/bytecode.hpp           |  7 +++++++
 include/mini_as/parser.hpp             |  2 +-
 include/mini_as/tokenizer.hpp          |  2 +-
 include/mini_as/type_checker.hpp       |  1 +
 src/bytecode.cpp                       | 19 +++++++++++++++++++
 src/parser.cpp                         |  7 ++++++-
 src/tokenizer.cpp                      |  4 ++--
 src/type_checker.cpp                   | 20 ++++++++++++++++++--
 tests/compat/cases/break_statements.as | 12 ++++++++++++
 tests/test_engine.cpp                  | 27 +++++++++++++++++++++++++++
 12 files changed, 111 insertions(+), 7 deletions(-)
```
