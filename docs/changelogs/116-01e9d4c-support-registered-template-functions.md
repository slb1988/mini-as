# feat(functions): support registered template functions

- Commit: 01e9d4c3253aeeec9c67d2483c3bd09d21407014
- Date: 2026-08-12 00:56:27 +0800
- Author: sunlaibing

## 变更内容

注册泛型函数与对象方法支持 AngelScript 2.38.0 风格模板语法，同时保持 callback-only 宿主边界：`T Identity<class T>(T value)`、`T choose<T, U>(T first, U second)`、`T Thing::echo<T>(T value) const`；脚本显式实例化，如 `Identity<int>(42)`。

- **编译模型**：注册模板定义把有序参数名存入 `FunctionSignature::templateParameters`；parser 只在限定函数/方法名已知是注册模板时才把 `<...>` 识别为模板实参表（普通比较表达式不受影响）；每次显式使用在类型检查前收集并物化为封闭签名（稳定 `FunctionId` + 具体 `templateArguments`）
- 替换目前支持模板参数直接用作值或 handle 类型（`T` 与 `T@`）；嵌套替换（如 `array<T>`）、实参类型推断、默认模板参数、模板特化刻意推迟；省略显式类型表不会选中模板定义；与普通注册重载冲突的封闭签名被拒绝而非静默替换
- 封闭实例参与普通重载、函数 handle、对象方法、反射与 bytecode 管线；开放定义只是 parser 输入与元数据，不进入普通可调用查找；同名同元数的多个模板定义照常实例化并竞争
- **运行时桥**：模板实例复用定义的 `GenericCall` 回调与宿主注册控制；`GetTemplateArgCount()`/`GetTemplateArgType(index)` 暴露具体类型表；实例引擎级缓存使各 module 共享稳定 id；bytecode 格式升至 version 8 持久化模板定义、封闭实参与显式 AST 使用，加载时在符号重映射前重建所需宿主实例

## 相关文档

- [docs/stages/92-registered-template-functions.md](../stages/92-registered-template-functions.md)

## 涉及文件

```
 AGENTS.md                                       |  14 ++
 CMakeLists.txt                                  |  10 ++
 docs/stages/92-registered-template-functions.md |  59 +++++++
 include/mini_as/engine.hpp                      |   5 +
 include/mini_as/generic.hpp                     |   6 +-
 include/mini_as/parser.hpp                      |  13 ++
 include/mini_as/type_checker.hpp                |   9 +-
 src/bytecode.cpp                                |  15 +-
 src/bytecode_io.cpp                             |  24 ++-
 src/engine.cpp                                  | 196 ++++++++++++++++++++++--
 src/generic.cpp                                 |  42 +++++-
 src/parser.cpp                                  |  64 +++++++-
 src/type_checker.cpp                            |  56 +++++--
 src/vm.cpp                                      |   9 +-
 tests/compat/cases/template_functions.as        |   3 +
 tests/compat/mini_runner.cpp                    |  10 ++
 tests/compat/official_runner.cpp                |  10 ++
 tests/test_generic.cpp                          | 154 +++++++++++++++++++
 tests/test_objects.cpp                          |  30 ++++
 19 files changed, 692 insertions(+), 37 deletions(-)
```
