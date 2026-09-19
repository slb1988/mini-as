# feat: define typed bytecode and disassembly

- Commit: 45d4a75955859b010f9f146e35c4ec010693080a
- Date: 2026-07-30 01:02:21 +0800
- Author: sunlaibing

## 变更内容

定义带类型的 bytecode 指令集与反汇编器，编译器开始把检查过的 AST 降级（lower）为常量池 + 定长指令：

- local 变量使用整数 slot；赋值编译为 `value, DUP, STORE`；数值 opcode 按类型区分（`ADD_I` vs `ADD_F`）；唯一支持的隐式转换由编译器显式插入 `TO_FLOAT`
- 每条指令保留源码位置——比 AngelScript 的压缩行号表更占空间，但让诊断、调试、suspension 与 bytecode 的关系一目了然；跳转操作数在本教学格式中是绝对指令索引，目标确定后通过 backpatch 回填
- 反汇编器作为一等调试工具与编译器一同测试，之后 VM 出错时总能对照实际执行的指令流

## 相关文档

- [docs/stages/06-bytecode.md](../stages/06-bytecode.md)

## 涉及文件

```
 CMakeLists.txt               |   2 +
 docs/stages/06-bytecode.md   |  16 +++
 include/mini_as/bytecode.hpp |  68 +++++++++++++
 src/bytecode.cpp             | 226 +++++++++++++++++++++++++++++++++++++++++++
 tests/test_bytecode.cpp      |  33 +++++++
 5 files changed, 345 insertions(+)
```
