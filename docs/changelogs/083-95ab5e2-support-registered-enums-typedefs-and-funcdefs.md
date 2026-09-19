# feat(host): support registered enums typedefs and funcdefs

- Commit: 95ab5e25841417f61d74e96b24f3855b34307278
- Date: 2026-08-09 22:05:26 +0800
- Author: sunlaibing

## 变更内容

宿主可注册此前只能出现在脚本 section 中的三类具名声明：`RegisterEnum`/`RegisterEnumValue`、`RegisterTypedef`、`RegisterFuncdef`。

- 这些是编译期注册而非运行时包装：解析 module 前引擎把名字注入 parser 的类型目录——注册 typedef 直接决议到原始类型，enum 保留具名 int32 身份，funcdef 按函数 handle 类型解析；同样的签名随后注入 `TypeChecker`、分配稳定 `TypeId`，并按需拷入不可变 module 镜像
- 注册枚举值走既有常量表达式查找与带类型整数常量；注册 funcdef 复用 `FunctionHandle`/`CallHandle` 与宿主函数描述符路径——handle 可指向脚本函数或 portable `GenericCall` 回调，无需新调用约定；宿主函数/方法/factory/属性声明现在在校验前归一化注册别名与枚举/funcdef 名
- 跨类别（宿主对象/enum/typedef/funcdef）的类型重名被拒绝；枚举值必须非空且 namespace 内唯一；typedef 仍限 AngelScript 原始数值与布尔类型；脚本声明不能遮蔽注册具名类型；不兼容的函数地址赋值是编译期诊断

## 相关文档

- [docs/stages/64-registered-enums-typedefs-funcdefs.md](../stages/64-registered-enums-typedefs-funcdefs.md)

## 涉及文件

```
 AGENTS.md                                          |  12 +++-
 CMakeLists.txt                                     |   7 +++
 docs/stages/64-registered-enums-typedefs-funcdefs.md |  37 +++++++++++
 include/mini_as/engine.hpp                         |  10 +++
 include/mini_as/parser.hpp                         |   3 +
 include/mini_as/type_checker.hpp                   |   6 ++
 src/engine.cpp                                     | 129 ++++++++++++++++++++++++++++++++++++++-
 src/parser.cpp                                     |  12 ++++
 src/type_checker.cpp                               |  56 ++++++++++++++++-
 tests/compat/cases/registered_named_types.as       |   6 ++
 tests/compat/mini_runner.cpp                       |  29 ++++++---
 tests/compat/official_runner.cpp                   |  22 ++++++-
 tests/test_generic.cpp                             |  64 ++++++++++++++++++++
 tests/test_parser.cpp                              |  17 ++++++
 14 files changed, 385 insertions(+), 25 deletions(-)
```
