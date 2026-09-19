# feat(host): support registered object methods

- Commit: 102bbe2e18516d9de3124408c990a25031d84938
- Date: 2026-08-09 21:23:06 +0800
- Author: sunlaibing

## 变更内容

宿主引用类型可经 portable generic 桥发布实例方法（如 `RegisterObjectMethod("HostRef", "int get() const", ...)`）：

- 声明解析接受官方尾部 `const` 限定并保留在持久声明中；注册方法只挂在属主类型上，不会意外成为全局函数；重载使用与脚本方法、全局宿主函数相同的转换、命名实参与引用参数规则
- 宿主方法元数据在 module 编译期挂到 stage 60 的宿主 `ClassSignature`；调用发射带 `HostMethod` 描述符的 `CallHost`（稳定 `FunctionId`/`TypeId`）；VM 把 receiver 与显式参数分别弹出，校验其运行时类型，经 `GenericCall::GetObject()` 暴露；`out`/`inout` 继续走 copy-in/copy-out
- null receiver、回调异常、返回值类型不符、非法输出写入保持为带位置 VM 异常；返回引用在注册期拒绝（等宿主属性存储提供持久引用目标）；指向注册方法的 delegate 被显式诊断而非误入脚本专用虚分派路径

## 相关文档

- [docs/stages/61-registered-object-methods.md](../stages/61-registered-object-methods.md)

## 涉及文件

```
 AGENTS.md                                       |  10 +++---
 CMakeLists.txt                                  |   7 +++++
 docs/stages/61-registered-object-methods.md     |  37 ++++++++++++++++++++++++
 include/mini_as/engine.hpp                      |   2 ++
 include/mini_as/generic.hpp                     |   4 +--
 src/bytecode.cpp                                |  11 +++++--
 src/engine.cpp                                  |  57 ++++++++++++++++++++++++++++++++++---
 src/generic.cpp                                 |   8 +++--
 src/type_checker.cpp                            |   8 +++--
 src/vm.cpp                                      |  11 +++++--
 tests/compat/cases/registered_object_methods.as |   4 +++
 tests/compat/mini_runner.cpp                    |  12 ++++++--
 tests/compat/official_runner.cpp                |   5 ++--
 tests/test_bytecode.cpp                         |  38 ++++++++++++++++++++++++
 tests/test_generic.cpp                          |  10 ++++++
 tests/test_objects.cpp                          | 118 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 17 files changed, 324 insertions(+), 19 deletions(-)
```
