# feat(classes): support reference casts

- Commit: 9833f146f5da46b116738db097fde628a4e7d352
- Date: 2026-08-09 11:40:01 +0800
- Author: sunlaibing

## 变更内容

支持 `cast<T>(expression)` 显式引用转换：

- 目标 `T` 必须是 script class 或 interface，表达式必须产出对象 handle，结果恒为 `T` 的 handle
- 与隐式 handle 转换（仅限派生→基、类→接口方向）不同，显式 cast 可在任意脚本对象 handle 类型间尝试；VM 检查 receiver 的真实 `TypeInfo`：类目标走基类链，接口目标查已实现接口表
- 转换成功保持同一个 `ObjectHandle`（身份与生命周期不变）；对象不兼容或源为 null 时返回 null handle，不抛异常
- bytecode 使用 `CastObject` + 稳定 `TypeId`，绝不保存可移动的元数据指针；module image 现在同时向 VM 暴露 class 与 interface 的 `TypeInfo`；对 null cast 结果的解引用由既有调用/字段 opcode 在使用点报告异常
- 本阶段只覆盖内置脚本层级 cast；用户自定义 `opCast`/`opImplCast` 属于运算符重载阶段

## 相关文档

- [docs/stages/47-reference-casts.md](../stages/47-reference-casts.md)

## 涉及文件

```
 AGENTS.md                             |  6 +++---
 CMakeLists.txt                        |  7 +++++++
 docs/stages/47-reference-casts.md     | 23 +++++++++++++++++++++++
 include/mini_as/bytecode.hpp          |  2 +-
 include/mini_as/parser.hpp            |  2 +-
 include/mini_as/tokenizer.hpp         |  2 +-
 src/bytecode.cpp                      | 10 +++++++++-
 src/engine.cpp                        |  6 ++++--
 src/parser.cpp                        | 12 ++++++++++++
 src/tokenizer.cpp                     |  3 ++-
 src/type_checker.cpp                  | 14 ++++++++++++++
 src/vm.cpp                            | 18 ++++++++++++++++++
 tests/compat/cases/reference_casts.as | 23 +++++++++++++++++++++++
 tests/test_bytecode.cpp               | 24 ++++++++++++++++++++++++
 tests/test_engine.cpp                 | 65 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                 | 14 ++++++++++++++
 tests/test_tokenizer.cpp              | 10 ++++++++++
 17 files changed, 230 insertions(+), 11 deletions(-)
```
