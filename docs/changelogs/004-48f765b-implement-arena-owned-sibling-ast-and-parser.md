# feat: implement arena owned sibling AST and parser

- Commit: 48f765b1903458ee31769d87bebfcea200ccb677
- Date: 2026-07-30 00:56:58 +0800
- Author: sunlaibing

## 变更内容

实现 recursive descent parser 与 AST。设计要点：

- 每个 precedence level 对应一个解析函数，调用图即文法：`a + b * c` 直接生成右子树为 `*` 的 `+` 节点，无需事后修正优先级
- 所有语法共用一个 `AstNode` 结构：节点由 arena 统一持有，通过 `firstChild/nextSibling` 表达树的形状，避免 C++ 继承层级——延续 AngelScript 紧凑的树设计，declaration、statement、expression 的遍历方式完全一致
- parser 在后续阶段能执行之前就先接受 function 和 class 的语法形态，刻意的两步走让语法、类型规则、运行时行为可以独立测试
- 错误恢复会前进到语句边界，一个 malformed 源文件可以报告多个错误而不是第一个就停下

## 相关文档

- [docs/stages/03-parser-and-ast.md](../stages/03-parser-and-ast.md)

## 涉及文件

```
 CMakeLists.txt                   |   1 +
 docs/stages/03-parser-and-ast.md |  15 ++
 include/mini_as/parser.hpp       |  87 ++++++++++++
 src/parser.cpp                   | 299 +++++++++++++++++++++++++++++++++++++++
 tests/test_main.cpp              |  10 ++
 5 files changed, 412 insertions(+)
```
