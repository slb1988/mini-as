# feat(classes): support script destructors

- Commit: 4bad11aea0a8c1892da82f5eb270d9009c0905a5
- Date: 2026-08-09 10:24:10 +0800
- Author: sunlaibing

## 变更内容

script class 支持声明一个无参析构函数 `~ClassName() { ... }`：

- parser 把析构器与普通方法区分；类型检查拒绝名字不匹配、带参数、缺函数体、重复声明与 interface 析构器；bytecode 用稳定 `TypeId`/`FunctionId` 链接析构器
- 释放最后一个 handle 不会直接进入 VM：`ScriptObject` 标记析构已调度、持有一个队列引用并提交到 engine finalizer 队列；VM 只在指令边界排空该队列——被调方的局部对象在调用方执行下一条指令前完成 finalize，避免从 `Release()` 重入 VM；调度标记保证 exactly-once，包括被循环回收发现的对象
- 每个入队对象持有一份包含其析构器等调用目标的不可变 bytecode 副本；module state 以弱引用观察，避免 module 全局与自身 finalizer 元数据成环；析构中可正常使用宿主调用与普通脚本调用；析构异常经引擎诊断回调上报（含位置），不替换触发 finalize 的脚本结果
- 当前收集器会在不可达环的入队析构器运行前清空对象 handle 字段：标量字段仍可用，观察环内其他成员在本阶段刻意不规定行为

## 相关文档

- [docs/stages/44-script-destructors.md](../stages/44-script-destructors.md)

## 涉及文件

```
 AGENTS.md                                |   6 +++---
 CMakeLists.txt                           |   7 +++++++
 docs/stages/44-script-destructors.md     |  27 +++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp             |   2 ++
 include/mini_as/engine.hpp               |  10 ++++++++--
 include/mini_as/object.hpp               |  25 ++++++++++++++++++++++---
 include/mini_as/parser.hpp               |   1 +
 include/mini_as/type_checker.hpp         |   1 +
 include/mini_as/vm.hpp                   |   8 ++++++++
 src/bytecode.cpp                         |  27 ++++++++++++++++++++-------
 src/engine.cpp                           |  68 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++--
 src/object.cpp                           |  20 ++++++++++++++++++--
 src/parser.cpp                           |  12 ++++++++++++
 src/type_checker.cpp                     |  17 +++++++++++++----
 src/vm.cpp                               |  38 ++++++++++++++++++++++++++++++++++----
 tests/compat/cases/script_destructors.as |  14 ++++++++++++++
 tests/test_bytecode.cpp                  |  21 +++++++++++++++++++++
 tests/test_engine.cpp                    |  82 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_gc.cpp                        |  19 +++++++++++++++++++
 tests/test_parser.cpp                    |  13 +++++++++++++
 20 files changed, 396 insertions(+), 22 deletions(-)
```
