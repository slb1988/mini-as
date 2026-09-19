# feat(classes): support field initializers

- Commit: 3b3aa375775a5b07dc1c89818b7762281c900699
- Date: 2026-08-08 13:08:40 +0800
- Author: sunlaibing

## 变更内容

class 字段支持赋值式 initializer：

- initializer 表达式在类作用域中检查，因此可以通过隐式 `this` 引用更早声明的字段与实例方法；类型不兼容是编译错误
- `ScriptObject` 先给出按类型的默认值；每个构造点处，编译器把新对象保存在隐藏 local 中，按声明顺序求值显式字段 initializer，然后再调用选中的构造器
- 运行时失败保留字段声明处的源码位置

## 相关文档

- [docs/stages/30-field-initializers.md](../stages/30-field-initializers.md)

## 涉及文件

```
 CMakeLists.txt                           |  7 +++++
 docs/stages/30-field-initializers.md     | 10 +++++++
 include/mini_as/bytecode.hpp             |  3 ++
 src/bytecode.cpp                         | 48 ++++++++++++++++++++++++++++----
 src/parser.cpp                           |  1 +
 src/type_checker.cpp                     | 10 ++++++-
 tests/compat/cases/field_initializers.as | 10 +++++++
 tests/test_engine.cpp                    | 30 ++++++++++++++++++++
 8 files changed, 113 insertions(+), 6 deletions(-)
```
