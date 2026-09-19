# feat: add two pass functions and explicit call frames

- Commit: 2f9b7dd2b6c0c82883dc89cd8cf1213397e0c20f
- Date: 2026-07-30 01:08:44 +0800
- Author: sunlaibing

## 变更内容

实现两遍（two-pass）函数编译与显式调用帧：

- builder 先为所有 script function 分配槽位并记录签名，再编译函数体——因此调用可以指向文件中稍后声明的函数，递归调用与普通调用没有任何区别
- `CALL` 把参数弹入新的 local slot，并把调用者的 function、pc、local 数组保存在堆上的 frame 中；`RET` 恢复该 frame 并把返回值压回调用者的表达式栈
- operand stack 与控制 frame 分离，简化清理与检查
- 调用深度限制是显式的，超限产生 script exception

测试覆盖前向递归，并验证嵌套调用后调用者的 locals 完好。

## 相关文档

- [docs/stages/10-functions-and-frames.md](../stages/10-functions-and-frames.md)

## 涉及文件

```
 docs/stages/10-functions-and-frames.md | 13 +++++++
 include/mini_as/bytecode.hpp           |  6 +++-
 include/mini_as/vm.hpp                 |  9 ++++-
 src/bytecode.cpp                       | 66 +++++++++++++++++++++++++++-------
 src/vm.cpp                             | 40 +++++++++++++++++----
 tests/test_engine.cpp                  | 27 ++++++++++++++
 6 files changed, 141 insertions(+), 20 deletions(-)
```
