# feat(templates): support registered template types

- Commit: 2a70f2a486465eda586dd72f2618df5eb3503cf4
- Date: 2026-08-11 21:17:03 +0800
- Author: sunlaibing

## 变更内容

宿主可注册 AngelScript 风格模板声明，module 从脚本使用中物化稳定的封闭类型：`RegisterObjectType("Box<class T>")`、`RegisterTemplateType("Pair<class K, class V>")`。

- **解析与规范身份**：parser 预先获得可见模板名与元数；接受原始、对象、handle、限定、多参数与嵌套子类型形式（含 `Pair<Box<int>, float>` 相邻闭括号）；每次使用归一化为规范名如 `Pair<Box<int>,float>`；tokenizer 仍发出 `>>`/`>>>` 移位 token，仅在消费嵌套模板闭括号时拆分——表达式移位语义不变；未知模板、空子类型表、元数不符产生带位置诊断
- **封闭实例与校验**：引擎在类型检查前惰性物化每个不同的封闭实例，各自获得稳定 `TypeId`/`TypeInfo`，重复使用返回同一指针；定义与实例暴露模板定义/实例状态、限定基名、参数名、具体 `DataType` 子类型，并经 `TypeMetadata` 可查；namespace/access mask/配置组控制从模板注册继承；可选 portable validator 在首次见到某封闭实例时运行一次，拒绝则使候选构建在使用点失败并保留旧镜像
- **宿主声明与动态编译**：generic 声明读取器理解限定与嵌套封闭模板类型，注册回调可经 `GenericCall` 收发封闭模板 handle；动态编译可引入新封闭实例而不丢失 module 既有环境
- 本阶段尚不注册子类型替换后的模板 factory/方法/属性/初始化列表行为——那些随 array 等 add-on 阶段到来，而非成为 VM 内置

## 相关文档

- [docs/stages/74-registered-template-types.md](../stages/74-registered-template-types.md)

## 涉及文件

```
 AGENTS.md                                       |   6 +-
 CMakeLists.txt                                  |   7 ++
 docs/stages/74-registered-template-types.md     |  81 ++++++++++
 include/mini_as/engine.hpp                      |  21 +++
 include/mini_as/object.hpp                      |   4 +
 include/mini_as/parser.hpp                      |  12 ++
 src/engine.cpp                                  | 202 ++++++++++++++++++++++++
 src/generic.cpp                                 |  71 ++++++++-
 src/parser.cpp                                  |  93 +++++++++--
 tests/compat/cases/registered_template_types.as |   6 +
 tests/compat/mini_runner.cpp                    |   4 +
 tests/compat/official_runner.cpp                |   8 +
 tests/test_engine.cpp                           |  95 +++++++++++
 tests/test_generic.cpp                          |  14 ++
 tests/test_parser.cpp                           |  33 ++++
 15 files changed, 637 insertions(+), 20 deletions(-)
```
