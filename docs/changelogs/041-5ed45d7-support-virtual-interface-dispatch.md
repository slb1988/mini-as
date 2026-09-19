# feat(classes): support virtual interface method dispatch

- Commit: 5ed45d75f030226dcb44ad4725eb8e8955bb31e7
- Date: 2026-08-08 13:17:11 +0800
- Author: sunlaibing

## 变更内容

实现 interface 方法的虚分派：

- interface 方法按声明顺序获得稳定 slot；通过 interface handle 调用时发射 `CALL_VIRTUAL`，携带 interface `TypeId` + slot，而非具体函数地址
- module image 内含以（具体 `TypeId`、interface `TypeId`、slot）为键的分派表；运行时 VM 读取 receiver 的真实类型，决议出实现的 `FunctionId`，进入普通 script method frame（receiver 仍是隐藏 0 号 local）
- 缺失实现是编译错误；null receiver 与缺失的运行时分派项是带位置的 VM 异常

## 相关文档

- [docs/stages/31-virtual-interface-dispatch.md](../stages/31-virtual-interface-dispatch.md)

## 涉及文件

```
 CMakeLists.txt                                   |  7 +++++
 docs/stages/31-virtual-interface-dispatch.md     | 11 +++++++
 include/mini_as/bytecode.hpp                     | 13 ++++++--
 src/bytecode.cpp                                 | 78 +++++++++++++++++++++++++++++++----
 src/vm.cpp                                       | 20 ++++++++++
 tests/compat/cases/virtual_interface_dispatch.as | 17 +++++++++
 tests/test_engine.cpp                            | 34 +++++++++++++++++
 7 files changed, 172 insertions(+), 8 deletions(-)
```
