# feat(exceptions): support try catch blocks

- Commit: 0fd9bee5aa881561a9eec3164f5039263148360b
- Date: 2026-08-09 17:23:35 +0800
- Author: sunlaibing

## 变更内容

支持 `try { ... } catch { ... }` 块：

- catch 块有自己的词法作用域，只在 try 块内发生运行时异常时执行；正常完成跳过 catch；parser 拒绝缺少 catch 的畸形语句
- 每个 bytecode function 记录不可变异常表项（半开 try 指令区间 + catch 目标），不保留任何裸 AST/指令指针；嵌套区间选择最小包围处理器（最近 catch 优先）；表驱动表示让正常执行路径无需 enter/leave-handler opcode
- VM 为每个调用帧跟踪 operand-stack 基线：异常发生时先查当前函数，再沿保存的调用点 pc 向调用者走；选中处理器之上的帧被释放，operand stack 恢复到该帧基线，从 catch 目标继续执行；可捕获算术、null receiver、宿主显式异常等，但不会捕获协作式 suspend/abort 状态
- 无处理器时保持原行为：以异常状态结束并保留原始失败位置与调用栈；被捕获的异常从公开 context 结果中清除；展开时跳过失败 `out`/`inout` 调用后的 copy-out，部分修改不会被发布

## 相关文档

- [docs/stages/50-try-catch.md](../stages/50-try-catch.md)

## 涉及文件

```
 AGENTS.md                       |  7 ++++---
 CMakeLists.txt                  |  7 +++++++
 docs/stages/50-try-catch.md     | 32 ++++++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp    |  7 +++++++
 include/mini_as/parser.hpp      |  3 ++-
 include/mini_as/tokenizer.hpp   |  3 ++-
 include/mini_as/vm.hpp          |  3 +++
 src/bytecode.cpp                | 13 +++++++++++++
 src/parser.cpp                  |  9 +++++++++
 src/tokenizer.cpp               |  6 ++++--
 src/type_checker.cpp            |  4 ++++
 src/vm.cpp                      | 58 ++++++++++++++++++++++++++++++++++++++++++++++++++++++----
 tests/compat/cases/try_catch.as | 28 ++++++++++++++++++++++++++++
 tests/test_bytecode.cpp         | 18 ++++++++++++++++++
 tests/test_engine.cpp           | 48 ++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp           | 12 ++++++++++++
 16 files changed, 248 insertions(+), 10 deletions(-)
```
