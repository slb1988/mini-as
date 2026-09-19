# feat(modules): support shared script entities

- Commit: d813010c09b60df12e2ef67279367ab25d8af7b1
- Date: 2026-08-11 22:46:38 +0800
- Author: sunlaibing

## 变更内容

class、interface、enum、funcdef 与全局函数可在多个 module 中声明为 `shared`：

- 匹配的声明获得相同的引擎级类型与函数 ID——对象可以跨过 import 函数边界，在另一个 module 中作为同一共享类/接口被消费；`shared` 在顶层按上下文解析（沿用官方 2.38 行为，不把既有以 `shared` 为变量名的脚本打破）
- **定义注册表**：每次成功构建为每个共享实体发布结构指纹（实体种类、限定名、类型与引用限定、成员、运算符、字面量、函数体；排除源码位置与空白）；后续 module 可重复相同定义（含重载），但字段布局/声明/函数体不同则在发布任何新镜像或元数据前被拒绝；发布是事务性的
- 共享全局函数与方法使用引擎级稳定 `FunctionId`；非共享函数保留 module 限定 ID；共享对象类型复用引擎稳定 `TypeInfo`；反射暴露 shared 标记
- **隔离规则**：共享代码可依赖原始类型、内置 string、宿主注册类型/函数与其他共享脚本实体；类型检查拒绝非共享的字段/参数/返回/局部/推断/funcdef 类型、继承非共享脚本类型、读写 module 全局、调用或取址非共享脚本函数；import 绑定同样拒绝既非宿主注册也非共享的对象/枚举/funcdef/弱引用类型
- bytecode 格式升至 version 4：持久化各类 shared 标记；加载时重建并校验共享指纹、把共享函数重映射到引擎级 ID，仍遵守「活 import 绑定须由宿主恢复」的规则

`external shared` 短形式刻意留给下一阶段；本阶段要求每个参与 module 提供匹配的完整定义。

## 相关文档

- [docs/stages/82-shared-script-entities.md](../stages/82-shared-script-entities.md)

## 涉及文件

```
 AGENTS.md                                |  19 ++--
 CMakeLists.txt                           |   7 ++
 docs/stages/82-shared-script-entities.md |  72 ++++++++++++++++
 include/mini_as/engine.hpp               |   2 +
 include/mini_as/parser.hpp               |   1 +
 include/mini_as/type_checker.hpp         |   9 ++-
 src/bytecode_io.cpp                      |  20 +++--
 src/engine.cpp                           | 146 ++++++++++++++++++++++++++++++-
 src/parser.cpp                           |  12 +++
 src/type_checker.cpp                     | 107 +++++++++++++++++++++--
 tests/compat/cases/shared_entities.as    |  14 +++
 tests/compat/mini_runner.cpp             |  19 ++--
 tests/compat/official_runner.cpp         |  19 ++--
 tests/test_engine.cpp                    | 123 ++++++++++++++++++++++++++
 tests/test_parser.cpp                    |  19 ++++
 tests/test_tokenizer.cpp                 |  10 +++
 16 files changed, 566 insertions(+), 33 deletions(-)
```
