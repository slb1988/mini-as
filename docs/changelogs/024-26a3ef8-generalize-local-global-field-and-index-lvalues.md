# refactor(compiler): generalize local global field and index lvalues

- Commit: 26a3ef82080b12033bf6f818078f7dd7d87b4e9b
- Date: 2026-08-08 12:08:39 +0800
- Author: sunlaibing

## 变更内容

lvalue 编译路径重构：引入 `LValueRef`（kind 涵盖 `Local/Global/Field/Index`，携带类型、变量/全局/字段标识与接收者表达式），用 `ResolveLValue` + `CompileLValueLoad`/`CompileLValueStore` 统一标识符读取与赋值发射。

此前赋值只特判了 local 与 member 两种形态，且各自内联类型转换逻辑；重构后读取与存储走同一决议入口，为全局变量、数组索引赋值等后续特性铺平道路，同时保留 `int → float` 的显式转换插入。

## 涉及文件

```
 include/mini_as/bytecode.hpp | 12 ++++++
 src/bytecode.cpp             | 94 +++++++++++++++++++++++++++++++++-----------
 tests/test_bytecode.cpp      | 25 ++++++++++++
 3 files changed, 107 insertions(+), 24 deletions(-)
```
