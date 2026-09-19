# feat: compile structured control flow and suspension points

- Commit: 883b1cd70c600157f67cad242382cf2857596ed8
- Date: 2026-07-30 01:05:16 +0800
- Author: sunlaibing

## 变更内容

把结构化控制流编译为非结构化跳转，并引入 suspension：

- backpatching：编译器为 `JZ` 先发出占位目标，编译完分支体后用分支体之后的第一条指令回填；while 循环记录条件位置，在循环体后发出向后的 `JMP`
- `&&` / `||` 复用同一跳转机制（短路求值）而非急切的布尔 opcode——左操作数已确定结果时跳过右操作数，测试用一处故意不可达的除零验证
- 每条语句以 `SUSPEND` 开头：正常快速路径只是一次标志检查；被请求挂起时 VM 带着完整的堆栈、locals 和 pc 返回，调用 `Continue` 从下一条指令继续，无需重建任何 C++ 调用栈

## 相关文档

- [docs/stages/08-control-flow-and-suspend.md](../stages/08-control-flow-and-suspend.md)

## 涉及文件

```
 docs/stages/08-control-flow-and-suspend.md | 15 ++++++++
 include/mini_as/bytecode.hpp               |  5 +--
 include/mini_as/vm.hpp                     |  7 +++-
 src/bytecode.cpp                           | 58 +++++++++++++++++++++++++++---
 src/vm.cpp                                 | 57 +++++++++++++++++++++++------
 tests/test_vm.cpp                          | 25 +++++++++++++
 6 files changed, 149 insertions(+), 18 deletions(-)
```
