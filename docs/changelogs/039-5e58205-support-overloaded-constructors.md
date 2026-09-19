# feat(classes): support overloaded constructors

- Commit: 5e582055817c9572a32f6610cea5cb8469018718
- Date: 2026-08-08 13:01:01 +0800
- Author: sunlaibing

## 变更内容

class 支持重载构造函数：

- 类体接受「名字与类相同、返回类型隐式」的构造器声明；重载按参数个数与既有转换成本规则选择；参数表重复或无匹配均为编译错误
- 对象构造发射 `NEW_OBJECT`，先保留一个 handle 作为表达式结果，再以新对象为隐藏 `this` 调用选中的构造器（作为 `ScriptMethod`）；构造器的 void 结果被丢弃，operand stack 上留下初始化完成的 handle
- 一旦声明任何构造器，隐式零参构造器即被禁用——除非显式提供零参重载（与 AngelScript/C++ 行为一致）

## 相关文档

- [docs/stages/29-overloaded-constructors.md](../stages/29-overloaded-constructors.md)

## 涉及文件

```
 CMakeLists.txt                                |  7 +++++++
 docs/stages/29-overloaded-constructors.md     | 13 +++++++++++++
 include/mini_as/parser.hpp                    |  1 +
 include/mini_as/type_checker.hpp              |  1 +
 src/bytecode.cpp                              | 44 +++++++++++++++++++++++++++++++----
 src/parser.cpp                                |  8 +++++++
 src/type_checker.cpp                          | 44 +++++++++++++++++++++++++++++------
 tests/compat/cases/overloaded_constructors.as | 12 +++++++++++
 tests/test_engine.cpp                         | 32 +++++++++++++++++++++++++++
 9 files changed, 147 insertions(+), 15 deletions(-)
```
