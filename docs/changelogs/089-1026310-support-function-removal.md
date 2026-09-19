# feat(module): support function removal

- Commit: 1026310d60881a9d72d60cba7ad9e694b3e7b1d9
- Date: 2026-08-11 12:44:05 +0800
- Author: sunlaibing

## 变更内容

支持从 module 活跃作用域移除全局脚本函数：`module->RemoveFunction(function)`。

- 移除是可见性操作而非立即销毁：下一个不可变 `ModuleImage` 记录被移除的 `FunctionId` 并从增量编译环境中擦除其签名——module 查找与函数元数据查找不再返回它，新编译代码无法决议其名字
- 可执行 bytecode 留在镜像中：既有函数保留旧调用描述符并继续决议到被移除的 id；旧镜像被保留，移除前获得的裸函数指针仍可安全传给 `Prepare`；后续增量编译把隐藏函数继续带入新镜像
- 移除后可编译同名同声明的新函数并获得新稳定 id：既有调用方继续调旧实现，新调用方决议到替代实现——匹配 AngelScript 2.38.0 文档化的增量构建行为
- 只有可见的 module 级脚本函数可移除；null、detached 函数、方法、宿主函数、已移除函数被拒绝且不发布新镜像；脚本类及其 finalizer 不受影响

## 相关文档

- [docs/stages/69-dynamic-function-removal.md](../stages/69-dynamic-function-removal.md)

## 涉及文件

```
 AGENTS.md                                  |  9 ++---
 docs/stages/69-dynamic-function-removal.md | 37 ++++++++++++++++++++
 include/mini_as/engine.hpp                 |  2 ++
 src/engine.cpp                             | 31 ++++++++++++++++-
 tests/compat/mini_runner.cpp               |  5 +++
 tests/compat/official_runner.cpp           | 10 ++++++
 tests/test_engine.cpp                      | 70 ++++++++++++++++++++++++++++++++++++++
 7 files changed, 160 insertions(+), 4 deletions(-)
```
