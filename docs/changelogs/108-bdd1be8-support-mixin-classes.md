# feat(classes): support mixin classes

- Commit: bdd1be83081b995c01606b5b4c0aac69940a58b6
- Date: 2026-08-11 23:19:49 +0800
- Author: sunlaibing

## 变更内容

支持 mixin class：可复用的部分类定义，不引入运行时类型——`class Concrete : Reusable {}` 展开 `mixin class Reusable` 的成员。

- mixin 不能被实例化、不能用作字段/参数/返回类型、不进入反射；不能 shared/external；不能有构造器、析构器、child funcdef；可实现接口但不能继承类或其他 mixin
- **AST 展开与优先级**：parser 记录专门的 `MixinDecl`，随后把被包含 mixin 的成员以 arena 持有的深拷贝展开进具体类；拷贝的成员保留原始源码位置，但以包含类为隐式 `this` 编译——mixin 方法可引用仅由目标类提供的字段或方法
- 显式类成员优先于同名 mixin 成员；mixin 方法视同派生类声明的方法，因而可 override 继承的基类方法；与显式或继承字段冲突的 mixin 字段（及其 initializer）被省略；mixin 列出的接口转移给包含类；展开在整个 module 解析完成后进行，因此类可包含声明在更后 section 或 namespace 中的 mixin
- bytecode 格式升至 version 6（持久化 mixin token、声明种类、展开成员标记与保留语法树）；无 mixin 专用运行时 opcode——展开后走普通类布局、类型检查、编译、虚分派、反射与 VM 路径

## 相关文档

- [docs/stages/84-mixin-classes.md](../stages/84-mixin-classes.md)

## 涉及文件

```
 AGENTS.md                           |  11 ++-
 CMakeLists.txt                      |   7 ++
 docs/stages/84-mixin-classes.md     |  49 +++++++++++
 include/mini_as/parser.hpp          |   7 +-
 include/mini_as/tokenizer.hpp       |   2 +-
 include/mini_as/type_checker.hpp    |   3 +
 src/bytecode.cpp                    |   5 +-
 src/bytecode_io.cpp                 |   8 +-
 src/parser.cpp                      | 163 +++++++++++++++++++++++++++++++++++-
 src/tokenizer.cpp                   |   5 +-
 src/type_checker.cpp                |  37 ++++++++-
 tests/compat/cases/mixin_classes.as |  15 ++++
 tests/test_engine.cpp               |  96 +++++++++++++++++++++
 tests/test_parser.cpp               |  26 ++++++
 tests/test_tokenizer.cpp            |   9 ++
 15 files changed, 429 insertions(+), 14 deletions(-)
```
