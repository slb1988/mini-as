# feat(language): support continue statements

- Commit: e7f491423cb9f011eb9a02a71bcd45d16fccf071
- Date: 2026-08-08 12:39:26 +0800
- Author: sunlaibing

## 变更内容

支持 `continue` 语句：

- 仅在循环内合法；类型检查器把循环深度与可 break 深度分开跟踪——因此单独的 switch 允许 `break` 但不允许 `continue`
- 编译器向外查找最近一个标记为循环的控制流 context，允许「循环内嵌套 switch 里写 `continue`」；待解析跳转按循环种类回填：`while`/`do-while` 回到条件，`for` 跳到 increment 段，与 AngelScript 循环语义一致

## 相关文档

- [docs/stages/24-continue-statements.md](../stages/24-continue-statements.md)

## 涉及文件

```
 CMakeLists.txt                            |  7 +++++++
 docs/stages/24-continue-statements.md     | 10 ++++++++++
 include/mini_as/bytecode.hpp              |  1 +
 include/mini_as/parser.hpp                |  2 +-
 include/mini_as/tokenizer.hpp             |  2 +-
 include/mini_as/type_checker.hpp          |  1 +
 src/bytecode.cpp                          | 24 ++++++++++++++++++++----
 src/parser.cpp                            |  6 ++++++
 src/tokenizer.cpp                         |  3 ++-
 src/type_checker.cpp                      |  9 +++++++++
 tests/compat/cases/continue_statements.as | 20 ++++++++++++++++++++
 tests/test_engine.cpp                     | 29 +++++++++++++++++++++++++++++
 12 files changed, 107 insertions(+), 7 deletions(-)
```
