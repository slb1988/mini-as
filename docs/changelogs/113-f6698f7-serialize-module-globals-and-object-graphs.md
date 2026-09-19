# feat(serialization): serialize module globals and object graphs

- Commit: f6698f736c468fbd460949da56785c6f6c2c0a99
- Date: 2026-08-12 00:04:17 +0800
- Author: sunlaibing

## 变更内容

把已构建 module 的活脚本全局状态独立于程序镜像持久化：`SaveBytecode()` 归档代码与元数据，新的 `SaveState()`/`LoadState()` 归档可变值与强可达对象图——新引擎可先加载 bytecode 再恢复存档的世界状态。

- 归档格式：`MASS` magic + 版本 1 + 有界负载 + FNV-1a 校验和；内容按序为：module 名与非宿主全局 schema、对象壳描述符与捕获 cell 计数、全局根值、对象负载与捕获 cell 负载
- 对象与捕获 cell 获得从 1 开始的归档 ID；壳在值解码前分配，因此前向引用、共享别名、闭包、环都能无需递归构造地重建；弱引用只在目标也被强可达时记录 ID——仅弱可达的目标不会被提升进保存图
- 函数 handle 使用稳定的 module 名、声明、对象类型、funcdef 名与分派类型拼写，而非进程局部数值 ID；`LoadBytecode()` 之后加载状态时向目标引擎新分配的 ID 决议
- 内置 codec 覆盖：标量、枚举、string、对象、弱引用、函数 handle 值；匿名函数捕获 cell；脚本对象（含继承平铺字段与析构器）；此前阶段的 `array`/`dictionary`/`any`/`ref`/`dictionaryValue`；任意其他注册引用/值类型以精确诊断拒绝（静默指针转储既不安全也不可移植）
- 事务与所有权：只归档脚本全局（注册宿主属性归宿主所有，恢复时保持现值）；完整 schema 校验通过后才原子交换根；旧图释放的 finalizer 在正常引擎安全点排空；操作单线程，`LoadState()` 期间宿主不得对同一 `ModuleState` 执行/恢复 context

## 相关文档

- [docs/stages/89-module-state-serialization.md](../stages/89-module-state-serialization.md)

## 涉及文件

```
 AGENTS.md                                    |   15 +-
 CMakeLists.txt                               |    2 +
 docs/stages/89-module-state-serialization.md |   68 ++
 include/mini_as/addons/array.hpp             |    2 +
 include/mini_as/compat.hpp                   |    2 +
 include/mini_as/engine.hpp                   |    2 +
 include/mini_as/object.hpp                   |    2 +
 src/compat.cpp                               |    6 +
 src/object.cpp                               |   7 +
 src/script_array.cpp                         |   2 +
 src/state_io.cpp                             | 1002 ++++++++++++++++++++++++++
 tests/test_serialization.cpp                 |  306 ++++++++
 12 files changed, 1414 insertions(+), 2 deletions(-)
```
