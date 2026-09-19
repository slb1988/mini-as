# feat(serialization): serialize suspended contexts

- Commit: 3099df250a1f5269816d2fc5904686bd8caa658d
- Date: 2026-08-12 00:20:25 +0800
- Author: sunlaibing

## 变更内容

持久化协作式挂起的 `ScriptContext`，并在新引擎加载对应 bytecode module 后恢复执行——把 stage 89 的图身份模型从 module 根扩展到完整活 VM 状态。

- `SaveState()` 只接受 `Suspended` 状态，`LoadState()` 只接受 `Uninitialized` 目标；`MASC` v1 归档包含：当前涉及的每个 module 镜像（初始函数、当前函数、保存的调用者帧）、各 module 的非宿主全局 schema 与活值、跨全局/操作数栈/locals/所有帧的共享对象与捕获 cell 图、初始与当前函数描述符、操作数栈、当前 locals、捕获、pc、栈基线、每个调用者函数及其返回 pc/locals/捕获/栈基线——import 函数因此可在提供方 module 挂起、恢复时回到消费方 module；module 状态一起恢复，别名全局的局部 handle 仍指向同一重建对象
- **稳定恢复点**：不归档裸进程指针与数值符号 ID；函数按 module 名 + 完整声明 + 对象类型识别；恢复点还记录函数代码长度及下一条指令的 opcode 与完整源码位置——加载时决议新指针与 ID，目标 bytecode 结构或源码 cue 不符则拒绝归档
- 刻意不保存宿主配置（line callback、context 池所有权、脚本函数决议器）——这些来自新目标 context 与引擎；finalizer 绑定、module 持有者、安全点回调从决议出的目标镜像重建
- 事务性：发布前校验全部 module schema、对象壳、值、函数目标、局部布局与恢复标记；失败清理候选并保留目标未初始化与已发布 module 状态；成功提交用不抛异常的根交换再安装挂起 VM 状态；单线程；一个已存 context 可跨多 module，但不得跨越同名 module 的两个历史镜像

## 相关文档

- [docs/stages/90-suspended-context-serialization.md](../stages/90-suspended-context-serialization.md)

## 涉及文件

```
 AGENTS.md                                         |  10 +-
 docs/stages/90-suspended-context-serialization.md |  60 +++
 include/mini_as/compat.hpp                        |   2 +
 include/mini_as/engine.hpp                        |   4 +
 include/mini_as/vm.hpp                            |   1 +
 src/compat.cpp                                    |   6 +
 src/engine.cpp                                    |  16 +
 src/state_io.cpp                                  | 692 +++++++++++++++++++++-
 tests/test_serialization.cpp                      | 279 +++++++++
 9 files changed, 1046 insertions(+), 24 deletions(-)
```
