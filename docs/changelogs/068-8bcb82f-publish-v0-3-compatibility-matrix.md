# docs: publish v0.3 compatibility matrix

- Commit: 8bcb82f5ba019ef27d983deabd1605c5ea712838
- Date: 2026-08-09 17:25:30 +0800
- Author: sunlaibing

## 变更内容

发布 v0.3 兼容性矩阵，取代 v0.2 版作为当前行为基线：

- 新增 Supported：完整整数族、float/double、位运算/移位/指数、默认参数、命名参数、`try/catch`、单继承、访问控制
- 标注 Supported subset：数值字面量、enum、typedef、namespace、运算符重载、属性访问器、引用参数/返回、析构、reference cast
- 明确 Intentional divergence：string 仍是内置教学值（官方经 add-on 提供）；函数对象（funcdef/handle/delegate/lambda/weakref）列入 v0.4，模板/add-on/module/coroutine/序列化列入 v0.6
- 更新差分覆盖清单与 CI 说明（GCC/Clang/MSVC + 差分 + ASan/UBSan）

## 相关文档

- [docs/compatibility-v0.3.md](../compatibility-v0.3.md)

## 涉及文件

```
 AGENTS.md                  |  6 +++---
 docs/compatibility-v0.3.md | 82 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 2 files changed, 85 insertions(+), 3 deletions(-)
```
