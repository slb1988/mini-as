# feat: finish context controls and tutorial clone

- Commit: 9e000ccf06f796afa45c670fc1a464b5d2b3feea
- Date: 2026-07-30 01:21:34 +0800
- Author: sunlaibing

## 变更内容

补齐 context 控制能力并完成教程克隆示例，教学引擎主线收官：

- line cue 现在先回调宿主再检查 Abort/Suspend——回调可以实现时间或指令预算，而无需插桩单个 opcode；suspension 保留全部 VM frame，abort 在下一个 cue 退出；运行时 trap 会快照当前函数与已保存的 call frame（含源码位置）
- 最终教程完整复刻 AngelScript 的经典流程：创建引擎 → 安装诊断 → 注册 `Print` 与 `GetSystemTime` → 添加脚本 section → 构建 module → 查找 `float calc(float, float)` → prepare context → 设置参数 → 执行 → 读取 float 返回值
- 端到端测试注入确定性时钟并捕获 Print 输出；另有回调驱动的 suspend/resume、回调 abort 与三级异常栈追踪测试
- 同时新增 `docs/architecture.md`，给出从 source section 到 GC 的整体架构图与「三类对象变化速度不同所以所有权分离」的核心论述

## 相关文档

- [docs/stages/15-tutorial-and-context.md](../stages/15-tutorial-and-context.md)
- [docs/architecture.md](../architecture.md)

## 涉及文件

```
 CMakeLists.txt                         |  6 ++++
 README.md                              | 51 +++++++++++++++++++++++++--
 docs/architecture.md                   | 19 ++++++++++
 docs/stages/15-tutorial-and-context.md | 16 +++++++++
 examples/tutorial/main.cpp             | 54 ++++++++++++++++++++++++++++
 examples/tutorial/script.as            |  7 ++++
 include/mini_as/engine.hpp             |  4 +++
 include/mini_as/vm.hpp                 |  8 +++++
 src/engine.cpp                         |  8 ++++-
 src/vm.cpp                             | 14 ++++++++
 tests/test_tutorial.cpp                | 64 ++++++++++++++++++++++++++++++++++
 11 files changed, 248 insertions(+), 3 deletions(-)
```
