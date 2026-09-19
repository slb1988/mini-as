# feat(functions): support function handles

- Commit: fa8b5245bc72e6b164d2a53862e21f73c682a032
- Date: 2026-08-09 18:09:14 +0800
- Author: sunlaibing

## 变更内容

`funcdef` 类型成为一等 function handle：

- `@function` 取匹配全局函数的地址，handle 可存入 local/module 全局/对象字段/参数/返回值，并用普通调用语法调用；匹配的注册宿主函数经同一 `GenericCall` 桥支持
- handle 按完整 funcdef 签名定型（返回类型与引用限定、参数类型、`in`/`out`/`inout` 模式）；重载全局函数在初始化、赋值、返回检查中按期望 funcdef 类型决议；与 2.38.0 一致，替换已有 handle 需显式句柄目标（`@callback = @multiply`）；null handle 可默认初始化、从 `null` 赋值、用 `is` 比较，调用 null 抛带位置运行时异常
- 运行时值含稳定 `FunctionId`、funcdef 的稳定 `TypeId` 与 script/host 判别位；`CallHandle` 读取动态目标，`CallableRef` 提供静态检查的参数个数与 funcdef 身份；任何 bytecode/运行时结构都不保存指向可移动容器的函数指针
- prepared context 继续持有不可变镜像，module 重建期间旧镜像的脚本函数 handle 依然有效；重建失败仍保留旧镜像及其全局 handle 状态
- 本阶段支持全局脚本函数与注册宿主函数；绑定实例方法是 stage 53 的 delegate；匿名函数、child funcdef、宿主侧反射留待后续

## 相关文档

- [docs/stages/52-function-handles.md](../stages/52-function-handles.md)

## 涉及文件

```
 AGENTS.md                              |   4 ++--
 CMakeLists.txt                         |   7 +++++++
 docs/stages/52-function-handles.md     |  38 ++++++++++++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp           |   5 ++++-
 include/mini_as/core.hpp               |  20 ++++++++++++++++++--
 include/mini_as/parser.hpp             |   1 +
 include/mini_as/type_checker.hpp       |   8 ++++++--
 src/bytecode.cpp                       |  62 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++--
 src/core.cpp                           |  18 ++++++++++++++++--
 src/engine.cpp                         |   1 +
 src/object.cpp                         |   2 ++
 src/parser.cpp                         |  16 +++++++++++++---
 src/type_checker.cpp                   | 157 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++---------
 src/vm.cpp                             |  53 ++++++++++++++++++++++++++++++++++++++++++++++++
 tests/compat/cases/function_handles.as |  20 ++++++++++++++++++++
 tests/test_engine.cpp                  |  91 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                  |  16 ++++++++++++++++
 tests/test_types.cpp                   |  14 ++++++++++++++
 18 files changed, 511 insertions(+), 22 deletions(-)
```
