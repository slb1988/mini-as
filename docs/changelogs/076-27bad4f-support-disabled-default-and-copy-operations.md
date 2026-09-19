# feat(classes): support disabled default and copy operations

- Commit: 27bad4f4f446b0d9529bf0f94066bf4e44657e08
- Date: 2026-08-09 20:37:00 +0800
- Author: sunlaibing

## 变更内容

script class 支持用官方尾部 `delete` 属性禁用三个编译器自动提供的对象操作：

```angelscript
class Locked {
    Locked() delete;
    Locked(const Locked &in other) delete;
    Locked &opAssign(const Locked &in other) delete;
}
```

- `delete` 保持为上下文相关的函数属性而非全局保留字（与 2.38.0 一致）；parser 同时接受官方拷贝签名需要的 `const Type &in` 参数拼写并在参数 AST 上记录 const
- 类型检查器在发布可调用方法前分类 deleted 声明；`ClassSignature` 独立记录默认构造、生成拷贝构造、生成拷贝赋值的删除状态；deleted 声明不会获得 `FunctionId`、bytecode 函数体或虚槽；删除拷贝构造同时抑制 stage 57 的生成拷贝路径
- 调用已删除的默认构造器、隐式使用已删除的基类默认构造器、删除拷贝后的生成拷贝、删除 `opAssign` 后的值赋值均为编译错误；其他签名的显式构造器仍可用；显式 handle 赋值（`@left = right`）保持合法——它是重新绑定 handle 而非拷贝对象状态
- 只有三个自动操作可被 delete；带函数体的 deleted 声明、重复删除、与显式定义并存均被拒绝
- 边界：教学运行时仍无生成的逐成员 `opAssign`；无重载时的普通对象赋值保持 handle-backed 模型

## 相关文档

- [docs/stages/58-deleted-default-operations.md](../stages/58-deleted-default-operations.md)

## 涉及文件

```
 AGENTS.md                                        | 13 ++++-
 CMakeLists.txt                                   |  7 +++
 docs/stages/58-deleted-default-operations.md     | 40 +++++++++++++++
 include/mini_as/parser.hpp                       |  1 +
 include/mini_as/type_checker.hpp                 |  3 ++
 src/bytecode.cpp                                 |  9 ++--
 src/parser.cpp                                   | 10 +++-
 src/type_checker.cpp                             | 85 ++++++++++++++++++++++++++++++--
 tests/compat/cases/deleted_default_operations.as | 22 +++++++++
 tests/test_bytecode.cpp                          | 17 +++++++
 tests/test_engine.cpp                            | 87 ++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                            | 21 +++++++++
 tests/test_types.cpp                             | 18 +++++++
 13 files changed, 318 insertions(+), 15 deletions(-)
```
