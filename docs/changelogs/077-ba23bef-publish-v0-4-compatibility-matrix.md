# docs: publish v0.4 compatibility matrix

- Commit: ba23bef97e1c05505fb18612ecef22f28af4ad6b
- Date: 2026-08-09 20:39:12 +0800
- Author: sunlaibing

## 变更内容

发布 v0.4 兼容性矩阵：在 v0.3 基础上新增函数对象（funcdef、function handle、delegate、匿名函数含捕获扩展、child funcdef、null 调用行为）、弱引用（`weakref`/`const_weakref`、生命周期竞争安全、GC 集成）与拷贝/默认操作生命周期规则（生成拷贝构造、删除默认/拷贝操作）的兼容状态。

同时声明 v0.5 边界：注册全局属性、引用类型 factory 与行为、对象方法/属性、值类型、反射与 `mini_as::compat` 门面；v0.6 边界：模板、array/dictionary/any/ref、初始化列表、索引、foreach、import、共享实体、增量 GC、context pooling、协程、序列化、变参、注册模板函数。原生 ABI 桥继续明确排除。

## 相关文档

- [docs/compatibility-v0.4.md](../compatibility-v0.4.md)

## 涉及文件

```
 AGENTS.md                  |  6 +++---
 docs/compatibility-v0.4.md | 88 ++++++++++++++++++++++++++++++++++++++++++++++++++++++
 2 files changed, 91 insertions(+), 3 deletions(-)
```
