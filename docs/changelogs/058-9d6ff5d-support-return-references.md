# feat(functions): support return references

- Commit: 9d6ff5d261f0a61496388c9eb6d02dc3dff14d4b
- Date: 2026-08-09 09:55:07 +0800
- Author: sunlaibing

## 变更内容

script function/method 支持返回引用：`int &access()`（可变）与 `const int &read()`（只读）。

- 编译器只接受指向 module 全局、经 module 全局可达的字段、以及当前方法 receiver 字段的引用；指向 local、参数、或只能经局部对象到达的字段被拒绝——因为函数返回后其存储可能消失
- VM 不暴露原生指针：`MAKE_GLOBAL_REF`/`MAKE_FIELD_REF` 创建 `ReferenceStorage` 描述符（稳定 global id，或 retained 对象 handle + 字段槽位）；普通表达式读取发 `LOAD_REF`，赋值/复合赋值保留描述符发 `STORE_REF`；前后缀自增复用同一动态 lvalue 路径；持有对象 handle 让返回的字段引用跨调用边界存活
- 只读返回引用参与 lvalue 检查：可读但不可作为赋值目标；经 null 全局 handle 构造字段引用等运行时失败报告 return 语句的位置
- 宿主返回引用继续被拒绝，直到注册宿主属性提供稳定存储抽象——避免接受 `GenericCall` 无法安全实现的声明

## 相关文档

- [docs/stages/43-return-references.md](../stages/43-return-references.md)

## 涉及文件

```
 AGENTS.md                               |   6 +++---
 CMakeLists.txt                          |   7 +++++++
 docs/stages/43-return-references.md     |  26 ++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp            |   8 ++++++--
 include/mini_as/core.hpp                |  15 +++++++++++++--
 include/mini_as/parser.hpp              |   5 +++--
 include/mini_as/type_checker.hpp        |   8 ++++++--
 src/bytecode.cpp                        | 113 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++----
 src/core.cpp                            |   9 +++++++++
 src/engine.cpp                          |   5 +++++
 src/generic.cpp                         |   8 ++++++++
 src/parser.cpp                          |  16 ++++++++++------
 src/type_checker.cpp                    |  67 ++++++++++++++++++++++++++++++++++++++++++++++++++++++--------
 src/vm.cpp                              |  53 ++++++++++++++++++++++++++++++++++++++++++++++++
 tests/compat/cases/return_references.as |  11 +++++++++++
 tests/test_bytecode.cpp                 |  19 +++++++++++++++++++
 tests/test_engine.cpp                   |  55 ++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                   |  15 +++++++++++++
 18 files changed, 407 insertions(+), 39 deletions(-)
```
