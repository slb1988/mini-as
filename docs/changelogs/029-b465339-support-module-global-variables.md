# feat(language): support module global variables

- Commit: b465339fcb0d80844dd72a4a0aa4ce524dee70dd
- Date: 2026-08-08 12:25:48 +0800
- Author: sunlaibing

## 变更内容

支持 module 级全局变量：

- 顶层变量声明在 module image 中分配稳定的 `GlobalId` slot；函数通过 `LOAD_GLOBAL`/`STORE_GLOBAL` 读写全局，bytecode 不保存指向可移动值容器的地址
- 每次成功构建会在不可变 bytecode 镜像旁创建一份全新的可变 `ModuleState`；initializer 在镜像链接后按声明顺序执行；同一镜像 prepare 出的多个 context 共享该 state，而持有旧镜像的 context 在重建后仍保留旧 state（与 `3d986f4` 的镜像机制衔接）
- 原始类型与 string 全局在显式 initializer 运行前获得确定的默认值；编译错误或全局初始化期间的运行时异常不会破坏上次成功的镜像与 state；const 全局复用 const local 的赋值保护

## 相关文档

- [docs/stages/19-module-globals.md](../stages/19-module-globals.md)

## 涉及文件

```
 CMakeLists.txt                       |  7 +++
 docs/stages/19-module-globals.md     | 16 +++++++
 include/mini_as/bytecode.hpp         | 19 ++++++++-
 include/mini_as/engine.hpp           |  4 ++
 include/mini_as/parser.hpp           |  1 +
 include/mini_as/type_checker.hpp     | 10 +++++
 include/mini_as/vm.hpp               |  5 ++-
 src/bytecode.cpp                     | 82 ++++++++++++++++++++++++++++++++----
 src/engine.cpp                       | 39 ++++++++++++++++-
 src/parser.cpp                       |  8 +++-
 src/type_checker.cpp                 | 48 ++++++++++++++++++++-
 src/vm.cpp                           | 25 +++++++++--
 tests/compat/cases/module_globals.as |  7 +++
 tests/test_bytecode.cpp              | 29 +++++++++++++
 tests/test_engine.cpp                | 61 +++++++++++++++++++++++++++
 15 files changed, 341 insertions(+), 20 deletions(-)
```
