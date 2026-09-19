# feat(host): support registered object properties

- Commit: 0bdec449b40a497182cfaf7e3eb75b10721784c2
- Date: 2026-08-09 21:32:32 +0800
- Author: sunlaibing

## 变更内容

宿主引用类型可用与普通类成员相同的脚本语法暴露字段（`object.property`）：

- 教学 API 用 portable getter/setter 回调代替内嵌 C++ 字节偏移，保住无原生 ABI 的边界；可变声明要求一对回调，`const type name` 只需 getter 且拒绝 setter
- 注册属性成为宿主 `FieldSignature` 与引擎持有的 `TypeInfo` 中的稳定指针；编译器刻意复用 `LoadField`/`StoreField`/`MakeFieldReference`；VM 按 receiver 真实类型选择脚本存储或宿主回调——赋值、复合赋值、自增自减、`out`/`inout` 回写共享既有 lvalue 机制
- 编译期拒绝对 const 属性的写入与输出引用；运行期校验 receiver 可用性与回调值类型；回调异常、getter 返回类型错误、null receiver、setter 失败都保留字段表达式的源码位置
- 对象 handle 可作为属性类型（其引用类型已注册时）；注册值类型与反射元数据留给后续

## 相关文档

- [docs/stages/62-registered-object-properties.md](../stages/62-registered-object-properties.md)

## 涉及文件

```
 AGENTS.md                                          |  10 +++---
 CMakeLists.txt                                     |   7 +++++
 docs/stages/62-registered-object-properties.md     |  39 ++++++++++++++++++++++++
 include/mini_as/engine.hpp                         |   4 +++
 include/mini_as/generic.hpp                        |   8 +++++
 include/mini_as/object.hpp                         |   2 ++
 include/mini_as/type_checker.hpp                   |   2 ++
 src/engine.cpp                                     |  61 ++++++++++++++++++++++++++++++++
 src/type_checker.cpp                               |  12 ++++++--
 src/vm.cpp                                         |  80 +++++++++++++++++++++++++++++++-------------
 tests/compat/cases/registered_object_properties.as |   5 +++
 tests/compat/mini_runner.cpp                       |  20 +++++++++---
 tests/compat/official_runner.cpp                   |   2 ++
 tests/test_bytecode.cpp                            |  27 +++++++++++++++
 tests/test_objects.cpp                             | 135 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 15 files changed, 383 insertions(+), 31 deletions(-)
```
