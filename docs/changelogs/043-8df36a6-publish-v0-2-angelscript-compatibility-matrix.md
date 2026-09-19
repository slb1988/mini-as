# docs: publish v0.2 AngelScript compatibility matrix

- Commit: 8df36a604185b310f3522b8ce21ee39fb153b4b0
- Date: 2026-08-08 13:19:58 +0800
- Author: sunlaibing

## 变更内容

发布 v0.2 兼容性矩阵文档：以表格形式逐项对比教学引擎与 AngelScript 2.38.0 的语言与对象模型子集——覆盖原始值、声明、const/auto、module 全局、表达式、控制流、class/interface、虚分派、嵌入生命周期、GC 等维度，每项标注 Supported / Partial / Not supported 及说明。

文档明确边界：只比较源码级行为，不含 SDK 头文件、原生 ABI 调用约定、bytecode 兼容；并列出 v0.2 之后路线图上仍排除的能力（更宽数值族、enum、typedef、namespace、继承、析构、运算符重载、异常、委托、宿主对象注册、反射、模板、addon、import、增量 GC、协程、序列化等）。

## 相关文档

- [docs/compatibility-v0.2.md](../compatibility-v0.2.md)

## 涉及文件

```
 docs/compatibility-v0.2.md | 99 ++++++++++++++++++++++++++++++++++++++++++++++
 1 file changed, 99 insertions(+)
```
