# feat(functions): support default arguments

- Commit: 886c695264ddd1f08f3e5691fedc18378b0c7452
- Date: 2026-08-09 08:41:10 +0800
- Author: sunlaibing

## 变更内容

function、method、constructor 的参数支持尾部默认表达式：

- 调用方提供全部必填参数即可成立；重载选择之后，被省略的实参在调用点、按被调方声明所在 namespace 编译
- 默认表达式走普通的表达式类型检查与 bytecode 编译，因此可以读取 module 全局，并产生普通的带位置运行时异常；callable 描述符仍记录完整参数个数，栈布局与显式调用一致
- `FunctionSignature::defaultArgumentCount` 是持久元数据，而表达式本身只在 module 编译期间由 AST 持有——AST 指针不会逃逸进 `ModuleImage`，保持重建快照安全

## 相关文档

- [docs/stages/40-default-arguments.md](../stages/40-default-arguments.md)

## 涉及文件

```
 CMakeLists.txt                          |  7 +++++++
 docs/stages/40-default-arguments.md     | 15 ++++++++++++++
 include/mini_as/bytecode.hpp            |  1 +
 include/mini_as/type_checker.hpp        |  1 +
 src/bytecode.cpp                        | 65 +++++++++++++++++++++++++++++++++++++++++++++++++++++++----
 src/parser.cpp                          |  7 +++++++
 src/type_checker.cpp                    | 26 +++++++++++++++++++++-----
 tests/compat/cases/default_arguments.as | 24 ++++++++++++++++++++++++
 tests/test_bytecode.cpp                 | 17 ++++++++++++++++
 tests/test_engine.cpp                   | 40 ++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                   | 23 +++++++++++++++++++++++
 tests/test_types.cpp                    | 13 +++++++++++++
 12 files changed, 226 insertions(+), 13 deletions(-)
```
