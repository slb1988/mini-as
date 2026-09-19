# feat(language): support multiple declarations in one statement

- Commit: 36ec6f057592efc65dab937cf0a30f3646be57e8
- Date: 2026-08-08 12:12:42 +0800
- Author: sunlaibing

## 变更内容

局部声明语句支持逗号分隔的多声明形式，匹配 AngelScript 语法：`int first = 1, second = first + 1, third;`

- parser 把多声明语句表示为 `DeclList`，其子节点是普通的 `VarDecl`——初始化、类型检查、符号声明与 bytecode 生成与一串独立声明完全一致
- initializer 从左到右检查与执行，因此同一语句中后面的声明可以引用前面的
- 同一词法作用域内重名仍然非法，包括同一逗号声明内部引入的重复

新增差分用例 `multiple_declarations.as` 与 parser/engine 测试。

## 相关文档

- [docs/stages/16-multiple-declarations.md](../stages/16-multiple-declarations.md)

## 涉及文件

```
 CMakeLists.txt                              |  7 +++++++
 docs/stages/16-multiple-declarations.md     | 13 +++++++++++++
 include/mini_as/parser.hpp                  |  2 +-
 src/bytecode.cpp                            |  4 ++++
 src/parser.cpp                              | 21 ++++++++++++++++-----
 src/type_checker.cpp                        |  4 ++++
 tests/compat/cases/multiple_declarations.as |  5 +++++
 tests/test_engine.cpp                       | 25 +++++++++++++++++++++++++
 tests/test_parser.cpp                       | 17 +++++++++++++++++
 9 files changed, 92 insertions(+), 6 deletions(-)
```
