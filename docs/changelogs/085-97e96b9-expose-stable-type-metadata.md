# feat(reflection): expose stable type metadata

- Commit: 97e96b92df09d0cb753de6960ab72c0b2ee9aabe
- Date: 2026-08-11 11:52:16 +0800
- Author: sunlaibing

## 变更内容

引擎为每个已发布的 object、enum、typedef、funcdef 暴露一份稳定的反射记录 `TypeMetadata`（支持按 name/id/index 三种查询）：

- 记录携带稳定 `TypeId`、名称、类别、host/script 来源，以及对象字段与方法、继承信息、枚举值、typedef 底层类型、funcdef 签名（按类别取用）
- 记录存放在引擎持有的 `std::deque` 中：新增类型或重建后重新发布不会改变记录地址，重建原地更新内容
- 宿主注册即时发布，后续方法/属性/枚举值注册刷新同一记录；脚本声明不同——编译器先分配持久 id，但只有解析、类型检查、bytecode 生成、链接、module 全局初始化全部成功后引擎才发布其元数据：重建失败时上次成功的 `ModuleImage` 与其反射视图一起保持完整
- 对照官方 2.38.0 的 `asITypeInfo` 查询族，mini 保持 RAII 风格紧凑值 API，官方风格查询留给 `mini_as::compat` 门面映射

## 相关文档

- [docs/stages/65-stable-type-metadata.md](../stages/65-stable-type-metadata.md)

## 涉及文件

```
 AGENTS.md                              |  11 +++---
 docs/stages/65-stable-type-metadata.md |  37 ++++++++++++++++++++
 include/mini_as/engine.hpp             |  28 +++++++++++++++
 src/engine.cpp                         | 114 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/compat/mini_runner.cpp           |   6 ++++
 tests/compat/official_runner.cpp       |  10 ++++++
 tests/test_engine.cpp                  |  86 ++++++++++++++++++++++++++++++++++++++++++++++++
 7 files changed, 288 insertions(+), 4 deletions(-)
```
