# feat(classes): support generated copy constructors

- Commit: d2aee3b890a9ae0d681ea23c097f8cfc74a78869
- Date: 2026-08-09 20:02:01 +0800
- Author: sunlaibing

## 变更内容

script class 获得隐式拷贝构造器——除非声明了任何单参数构造器（遵循 2.38.0 的生成规则：抑制生成的构造器不必接受类自身类型）。

- `ClassSignature::generatedCopyConstructor` 只记录决定，不虚构 AST 函数体或不稳定调用目标；重载决议选中生成操作时，编译器发射 `NewObject` + `CopyObject`
- VM 用稳定目标 `TypeId` 校验源与目标对象，然后拷贝目标类的平铺字段区间——派生源可以按普通字段访问使用的继承字段前缀切片（slice）进基类目标
- 拷贝是逐成员的：整数/浮点/布尔/枚举/字符串独立拷贝；对象与函数 handle 仍指向同一目标；弱引用保留同一非持有 token；新副本不执行字段 initializer 与默认/基类构造器（源字段直接初始化目标）；null 源在构造调用点抛 VM 异常
- 由于 mini 仍以引用计数 handle 建模脚本实例，本阶段通过既有 `Class(source)` 表达式与 `Class@` 存储形式暴露生成拷贝；完整值对象存储仍在教学运行时对象模型之外

## 相关文档

- [docs/stages/57-generated-copy-constructors.md](../stages/57-generated-copy-constructors.md)

## 涉及文件

```
 AGENTS.md                                         |  4 ++--
 CMakeLists.txt                                    |  7 +++++++
 docs/stages/57-generated-copy-constructors.md     | 36 ++++++++++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp                      |  2 +-
 include/mini_as/object.hpp                        |  1 +
 include/mini_as/type_checker.hpp                  |  1 +
 src/bytecode.cpp                                  | 13 +++++++++++--
 src/object.cpp                                    |  9 +++++++++
 src/type_checker.cpp                              | 10 +++++++++-
 src/vm.cpp                                        | 19 +++++++++++++++++++
 tests/compat/cases/generated_copy_constructors.as | 31 +++++++++++++++++++++++++++++++
 tests/test_bytecode.cpp                           | 26 ++++++++++++++++++++++++++
 tests/test_engine.cpp                             | 55 +++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_types.cpp                              | 16 ++++++++++++++++
 14 files changed, 225 insertions(+), 5 deletions(-)
```
