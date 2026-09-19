# feat: execute expressions on a heap backed stack vm

- Commit: 4e594733225e79c0153150f88405a41f9c94eb63
- Date: 2026-07-30 01:03:49 +0800
- Author: sunlaibing

## 变更内容

实现堆 backed 的 stack VM，取代 tree-walking 执行表达式：

- VM 持有 value stack、local slot 数组和 program counter；dispatch 循环是 `while (active) switch(opcode)`，不在 C++ 调用栈上递归——这一分离是 script call frame、suspension、栈检查和确定性限制的前提
- 带类型的 opcode 让热路径直接：`ADD_I` 无需运行期重载决议即可读取两个整数
- 可能失败的指令先定位自身确切的指令位置，再把 VM 置为 `Exception` 状态——宿主拿到的是稳定的机器状态，而不是一个 C++ 异常

测试直接执行编译器的真实输出（而非手写指令），覆盖混合数值计算与除零失败路径。

## 相关文档

- [docs/stages/07-stack-vm.md](../stages/07-stack-vm.md)

## 涉及文件

```
 CMakeLists.txt             |   2 +
 docs/stages/07-stack-vm.md |  14 +++++
 include/mini_as/vm.hpp     |  38 ++++++++++++
 src/bytecode.cpp           |   3 +-
 src/vm.cpp                 | 149 +++++++++++++++++++++++++++++++++++++++++++++
 tests/test_vm.cpp          |  36 +++++++++++
 6 files changed, 241 insertions(+), 1 deletion(-)
```
