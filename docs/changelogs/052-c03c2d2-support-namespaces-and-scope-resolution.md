# feat(language): support namespaces and scope resolution

- Commit: c03c2d2a43dcf11af35207cf42f846a6dec3ea1d
- Date: 2026-08-09 08:37:07 +0800
- Author: sunlaibing

## 变更内容

支持具名与嵌套 `namespace` 块：

- function、global、class、interface、enum、typedef 获得规范化限定名（如 `Root::Left::read`），AST 保留 `NamespaceDecl` 层级供工具与诊断使用
- 非限定引用按「当前 namespace → 各父 namespace → 全局 namespace」顺序查找；`::` 运算符显式选择限定的函数、全局、枚举值或类型；类型检查与 bytecode 使用同一候选顺序，保证两个阶段的重载选择不会悄悄分叉
- 限定名同时是分配稳定 `TypeId`/`FunctionId`/`GlobalId` 的键——不同 namespace 可声明同名实体而不冲突，同一 namespace 内重复声明仍是诊断

## 相关文档

- [docs/stages/39-namespaces.md](../stages/39-namespaces.md)

## 涉及文件

```
 CMakeLists.txt                   |   7 +++++
 docs/stages/39-namespaces.md     |  17 +++++++++++
 include/mini_as/bytecode.hpp     |   1 +
 include/mini_as/parser.hpp       |   9 ++++--
 include/mini_as/tokenizer.hpp    |   4 +--
 include/mini_as/type_checker.hpp |   2 ++
 src/bytecode.cpp                 | 107 ++++++++++++++++++++++++++++++++-----------------------
 src/parser.cpp                   | 137 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-------
 src/tokenizer.cpp                |   9 +++--
 src/type_checker.cpp             | 127 +++++++++++++++++++++++++++++++++++++++++++++++----------
 tests/compat/cases/namespaces.as |  32 +++++++++++++++++
 tests/test_bytecode.cpp          |  25 +++++++++++++
 tests/test_engine.cpp            |  34 ++++++++++++++++++
 tests/test_parser.cpp            |  20 +++++++++++
 tests/test_tokenizer.cpp         |  10 +++++
 tests/test_types.cpp             |  17 +++++++++
 16 files changed, 486 insertions(+), 72 deletions(-)
```
