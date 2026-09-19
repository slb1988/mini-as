# feat(language): support do while loops

- Commit: ce087eb1dcb6efe35b2dab45f98e79919388b810
- Date: 2026-08-08 12:30:44 +0800
- Author: sunlaibing

## 变更内容

支持 `do`/`while` 循环：AST 中 body 在前、condition 在后，镜像其执行顺序。

- 末尾分号是语法的一部分，缺失时产生针对性的 parser 诊断
- 类型检查要求 bool 条件
- bytecode 直接进入循环体，每次迭代后求值条件：为 false 退出，否则跳回循环体——保证初始条件为 false 时也执行一次

## 相关文档

- [docs/stages/21-do-while-loops.md](../stages/21-do-while-loops.md)

## 涉及文件

```
 CMakeLists.txt                       |  7 +++++++
 docs/stages/21-do-while-loops.md     | 10 ++++++++++
 include/mini_as/parser.hpp           |  3 ++-
 include/mini_as/tokenizer.hpp        |  2 +-
 src/bytecode.cpp                     | 10 ++++++++++
 src/parser.cpp                       | 15 ++++++++++++++-
 src/tokenizer.cpp                    |  5 +++--
 src/type_checker.cpp                 |  6 ++++++
 tests/compat/cases/do_while_loops.as |  6 ++++++
 tests/test_engine.cpp                | 26 ++++++++++++++++++++++++++
 10 files changed, 85 insertions(+), 5 deletions(-)
```
