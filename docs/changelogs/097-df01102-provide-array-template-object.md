# feat(addon): provide array template object

- Commit: df01102d4427008d2ca06da671e0c60941c93f63
- Date: 2026-08-11 21:25:46 +0800
- Author: sunlaibing

## 变更内容

首个标准 add-on `array<class T>` 以普通注册模板类型（而非 VM 原语）落地：`mini_as::addons::RegisterScriptArray(*engine)`。

- `ScriptArray` 是持有带类型 `Value` 元素的引用计数 `RefObject`；每个封闭实例由 stage 74 的模板实例回调惰性配置，注册 portable `GenericCall` factory 与方法：默认/`uint length` 构造、`length()`/`isEmpty()`、`resize(uint)`、`insertLast(T)`/`removeLast()`、显式 `get(uint)`/`set(uint, T)`
- 原始、枚举、string、函数、弱引用、宿主值与对象 handle 元素复用既有 `Value` 拷贝/生命周期语义；引用对象元素目前必须以 handle（`T@`）拼写——mini 对象模型尚不能按值构造任意注册引用对象
- 数组实例参与既有循环收集器：枚举直接/经 delegate/经捕获 cell 持有的对象引用，回收环时清理元素——脚本对象与数组可构成可回收环；模板实例回调继承定义的 access mask 与配置组，实例配置失败时部分注册被停用
- bytecode 加载现在在稳定 ID 重映射与宿主回调重绑定之前物化归档的封闭宿主模板实例：使用 `array<int>` 的 module 可保存、加载进新配置引擎并直接执行，无需先编译源码模块来「预热」该特化
- 越界 `get`/`set`、空数组 `removeLast` 成为脚本调用点处的带位置 VM 异常；不支持的元素子类型在候选构建期的模板使用点被拒绝
- 初始化列表 factory 与 `[]` 语法刻意留给下两个独立语言提交

## 相关文档

- [docs/stages/75-array-template-object.md](../stages/75-array-template-object.md)

## 涉及文件

```
 AGENTS.md                                   |   7 +-
 CMakeLists.txt                              |  11 ++
 docs/stages/75-array-template-object.md     |  64 +++++++++++
 include/mini_as/addons/array.hpp            |  32 ++++++
 include/mini_as/engine.hpp                  |   8 +-
 include/mini_as/object.hpp                  |   3 +-
 src/engine.cpp                              |  75 +++++++++++--
 src/object.cpp                              |   5 +-
 src/script_array.cpp                        | 161 ++++++++++++++++++++++++++++
 tests/compat/cases/array_template_object.as |   7 ++
 tests/compat/mini_runner.cpp                |   4 +
 tests/compat/official_runner.cpp            |  14 ++-
 tests/test_addons.cpp                       | 114 ++++++++++++++++++++
 13 files changed, 490 insertions(+), 15 deletions(-)
```
