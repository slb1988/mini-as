# feat(reflection): expose stable function metadata

- Commit: 48bad159704f022be7d58eacb095eb6f9e30875a
- Date: 2026-08-11 12:12:13 +0800
- Author: sunlaibing

## 变更内容

引擎为宿主与脚本可调用体暴露持久反射记录 `FunctionMetadata`：

- 记录携带稳定 `FunctionId`、所属 module 名与完整 `FunctionSignature`（区分全局函数、方法、构造器、析构器、factory、宿主回调、参数模式、默认值与引用返回）；查询支持 `module->GetFunctionMetadataByDecl`、`engine->GetFunctionMetadataById/ByIndex`
- 记录存于引擎持有的 `std::deque`：重建函数原地刷新既有记录，缓存的元数据指针与 id 保持有效
- 宿主全局函数、对象 factory、对象方法注册成功即发布；脚本全局函数与类可调用体只在解析→类型检查→bytecode→链接→全局初始化全部成功后发布——失败的重建不会暴露被拒绝的签名，也不会改动上次成功的反射视图
- `GetFunctionMetadataByDecl` 是既有 `GetFunctionByDecl` 的反射对应物：经当前不可变镜像决议后返回引擎持有的稳定记录；移除/退休语义留给动态函数移除阶段

## 相关文档

- [docs/stages/66-stable-function-metadata.md](../stages/66-stable-function-metadata.md)

## 涉及文件

```
 AGENTS.md                                  |  13 ++++++-----
 docs/stages/66-stable-function-metadata.md |  40 +++++++++++++++++++++++++++++
 include/mini_as/engine.hpp                 |  12 +++++++++
 src/engine.cpp                             |  40 +++++++++++++++++++++++++++++
 tests/compat/mini_runner.cpp               |   9 +++++--
 tests/compat/official_runner.cpp           |   4 +--
 tests/test_engine.cpp                      |  71 ++++++++++++++++++++++++++++++++++++++++++++++++++++
 7 files changed, 182 insertions(+), 7 deletions(-)
```
