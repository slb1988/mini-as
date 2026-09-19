# feat(language): support enums

- Commit: debc993a9d90bcfb07107ebb0ef6c8d3df54c1b5
- Date: 2026-08-09 08:17:37 +0800
- Author: sunlaibing

## 变更内容

支持 script enum：

- enum 是具名的 32 位整数类型；枚举项从 0 开始逐项加一，也可使用引用前面枚举值的整数常量表达式；枚举名作为 module 常量可见且不可赋值
- parser 先发现 enum 类型名再解析声明，记录 `EnumDecl`/`EnumValue` 节点；类型检查器求值并做范围检查，在 `DataType` 中保留 enum 名，暴露稳定的 enum 元数据；bytecode 把枚举值作为带类型常量嵌入，复用现有整数算术与比较指令
- enum 可转换为内置数值类型（符合其整数常量的角色），但任意整数不会隐式转回 enum——误赋值可被诊断，且不需要单独的 VM 执行路径

## 相关文档

- [docs/stages/37-enums.md](../stages/37-enums.md)

## 涉及文件

```
 CMakeLists.txt                         |  7 +++++++
 docs/stages/37-enums.md                | 16 ++++++++++++++++
 include/mini_as/bytecode.hpp           |  4 +++-
 include/mini_as/constant_evaluator.hpp |  7 +++++++
 include/mini_as/core.hpp               |  4 +++-
 include/mini_as/parser.hpp             |  5 ++++-
 include/mini_as/tokenizer.hpp          |  2 +-
 include/mini_as/type_checker.hpp       | 15 +++++++++++++++
 src/bytecode.cpp                       | 28 ++++++++++++++++++++++++----
 src/constant_evaluator.cpp             |  6 ++++++
 src/core.cpp                           | 15 +++++++++++----
 src/engine.cpp                         |  4 +++-
 src/object.cpp                         |  1 +
 src/parser.cpp                         | 36 ++++++++++++++++++++++++++++++++----
 src/tokenizer.cpp                      |  4 +++-
 src/type_checker.cpp                   | 92 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-----
 tests/compat/cases/enums.as            | 19 +++++++++++++++++++
 tests/test_bytecode.cpp                | 20 ++++++++++++++++++++
 tests/test_engine.cpp                  | 38 ++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                  | 20 ++++++++++++++++++++
 tests/test_tokenizer.cpp               | 10 ++++++++++
 tests/test_types.cpp                   | 18 ++++++++++++++++++
 22 files changed, 346 insertions(+), 25 deletions(-)
```
