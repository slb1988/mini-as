# feat(reflection): expose module global metadata

- Commit: 44b481c12cc96d31598a92b00a15ae7148dc2ff0
- Date: 2026-08-11 12:24:37 +0800
- Author: sunlaibing

## 变更内容

script module 暴露自身全局变量的稳定反射记录 `GlobalMetadata`：

- 记录携带持久 `GlobalId`、所属 module 名与 `GlobalSignature`；`GlobalSignature::Declaration()` 提供声明查询用的规范 `[const] type name` 拼写；namespace 中的全局保留全限定名；查询支持 ByDecl/ByName/ById/ByIndex 四种
- 记录存于引擎持有的 `std::deque`：成功重建按 id 原地刷新，新增全局不会使缓存指针失效；module 查询先检查当前不可变 `ModuleImage`——已移除或外来全局不会经 module 意外暴露
- 发布时机与函数/类型元数据一致：全部构建阶段成功后才发布；失败的重建把旧镜像、共享全局状态、声明与元数据记录作为一个原子视图保留
- 注册宿主全局属性刻意排除在外（对齐 2.38.0：`asIScriptModule::GetGlobalVar*` 只枚举脚本全局，引擎级注册属性是另一套 API）；直接存储地址与变量移除留给后续兼容性与动态模块管理特性

## 相关文档

- [docs/stages/67-module-global-metadata.md](../stages/67-module-global-metadata.md)

## 涉及文件

```
 AGENTS.md                                |  9 ++++--
 docs/stages/67-module-global-metadata.md | 39 ++++++++++++++++++++++++++++++++
 include/mini_as/engine.hpp               | 14 ++++++++++++
 include/mini_as/type_checker.hpp         |  2 ++
 src/engine.cpp                           | 55 ++++++++++++++++++++++++++++++++++++++++++++++++
 src/type_checker.cpp                     |  4 ++++
 tests/compat/mini_runner.cpp             |  6 ++++++
 tests/compat/official_runner.cpp         |  7 ++++++
 tests/test_engine.cpp                    | 51 +++++++++++++++++++++++++++++++++++++++++++++
 9 files changed, 184 insertions(+), 3 deletions(-)
```
