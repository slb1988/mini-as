# feat(functions): support funcdef declarations

- Commit: 0941642c7a2a6e78051f6ae79d26dc95313718c0
- Date: 2026-08-09 17:33:58 +0800
- Author: sunlaibing

## 变更内容

支持 AngelScript 的 `funcdef` 语法，在全局与 namespace 作用域声明函数签名（如 `funcdef bool Filter(int, int);`）：

- 保留返回类型、const/可变返回引用、原始与对象参数、`in`/`out`/`inout` 参数模式；参数名可选（与官方一致）
- parser 把 funcdef 表示为专门声明（而非无函数体的可执行函数）；类型检查器发布含规范 `FunctionSignature` 与稳定 `TypeId` 的 `FuncdefSignature`；重名、与现有脚本类型冲突、默认参数均被拒绝；namespace 内名字保留限定身份
- module 构建成功时把不可变 funcdef 元数据拷入 `BytecodeModule`；engine 经与 class/interface/enum/typedef 相同的稳定类型注册表分配 ID——重建 module 不改变未变 funcdef 的身份；声明本身不产生可执行 bytecode 函数
- 本阶段刻意只建立「函数类型」描述：持有 funcdef handle 的变量与参数、`@function` 表达式、间接调用与 null 行为属于下一阶段（052 之后是 070 的 function handle）

## 相关文档

- [docs/stages/51-funcdefs.md](../stages/51-funcdefs.md)

## 涉及文件

```
 AGENTS.md                        |  4 ++--
 CMakeLists.txt                   |  7 +++++++
 docs/stages/51-funcdefs.md       | 29 +++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp     |  4 +++-
 include/mini_as/parser.hpp       |  3 ++-
 include/mini_as/tokenizer.hpp    |  2 +-
 include/mini_as/type_checker.hpp |  9 +++++++++
 src/bytecode.cpp                 |  4 +++-
 src/engine.cpp                   |  4 +++-
 src/parser.cpp                   | 36 ++++++++++++++++++++++++++++++++++++
 src/tokenizer.cpp                |  5 +++--
 src/type_checker.cpp             | 36 +++++++++++++++++++++++++++++++++++-
 tests/compat/cases/funcdefs.as   |  9 +++++++++
 tests/test_engine.cpp            | 41 +++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp            | 19 +++++++++++++++++++
 tests/test_tokenizer.cpp         |  8 ++++++++
 tests/test_types.cpp             | 16 ++++++++++++++++
 17 files changed, 226 insertions(+), 10 deletions(-)
```
