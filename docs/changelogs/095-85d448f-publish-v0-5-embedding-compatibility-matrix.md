# docs: publish v0.5 embedding compatibility matrix

- Commit: 85d448ffb3a91d898bc36135e9e624c10344d7b3
- Date: 2026-08-11 21:04:15 +0800
- Author: sunlaibing

## 变更内容

发布 v0.5 嵌入兼容性矩阵：在 v0.4 语言子集之上覆盖宿主注册（全局属性、引用类型 factory、对象方法/属性、值类型、enum/typedef/funcdef、namespace、access mask、配置组）、稳定反射（类型/函数/全局元数据与指针稳定性）、模块与 bytecode（事务性重建、动态编译/移除、MASB v2 私有格式 save/load，明确不做官方 bytecode 兼容）、调试检查（行回调、调用栈、局部变量快照、异常快照）、compat facade 各区状态。

并注明一个刻意差异：mini 的移除模型比官方 save/load 路径保留更多旧镜像状态，因此差分 save/load 用例只比较移除前的公共状态。此时套件共 56 个 CTest 条目（54 个差分用例）。v0.6 边界：注册模板类型与 array add-on 起步，随后初始化列表、索引、foreach、dictionary/any/ref、import、共享实体、mixin、增量 GC、context pooling、协程、活状态序列化、变参与注册模板函数。

## 相关文档

- [docs/compatibility-v0.5.md](../compatibility-v0.5.md)

## 涉及文件

```
 docs/compatibility-v0.5.md | 118 ++++++++++++++++++++++++++++++++++++++++++++++
 1 file changed, 118 insertions(+)
```
