# feat(language): support const variables and assignment protection

- Commit: 81592e390c476f020e7b65718175c3644f70a513
- Date: 2026-08-08 12:15:25 +0800
- Author: sunlaibing

## 变更内容

局部变量支持 `const` 限定：

- initializer 的检查与发射与普通声明一致，但类型检查器在该符号剩余的词法生命周期内将其标记为只读
- 对 const local 的赋值在生成 bytecode 之前就被拒绝；同样的保护沿字段访问传播——通过 const 变量拿到的对象不能经由该表达式被修改
- const 声明可使用 stage 16 引入的逗号分隔形式；与 AngelScript 一致，const 值在普通表达式中可读，也可被后续声明使用

新增差分用例 `const_variables.as` 与 tokenizer/engine 测试。

## 相关文档

- [docs/stages/17-const-variables.md](../stages/17-const-variables.md)

## 涉及文件

```
 CMakeLists.txt                        |  7 +++++++
 docs/stages/17-const-variables.md     | 13 +++++++++++++
 include/mini_as/parser.hpp            |  1 +
 include/mini_as/tokenizer.hpp         |  2 +-
 include/mini_as/type_checker.hpp      | 12 +++++++++---
 src/parser.cpp                        |  5 ++++-
 src/tokenizer.cpp                     |  4 ++--
 src/type_checker.cpp                  | 23 ++++++++++++++++++-----
 tests/compat/cases/const_variables.as |  4 ++++
 tests/test_engine.cpp                 | 22 ++++++++++++++++++++++
 tests/test_tokenizer.cpp              |  9 +++++++++
 11 files changed, 90 insertions(+), 12 deletions(-)
```
