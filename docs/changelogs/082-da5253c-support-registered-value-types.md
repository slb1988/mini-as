# feat(host): support registered value types

- Commit: da5253c463092743340b77355673faa0708cf39b
- Date: 2026-08-09 21:50:00 +0800
- Author: sunlaibing

## 变更内容

宿主可暴露可拷贝 C++ 值类型而不变成引用计数对象：`engine->RegisterValueType("HostNumber", Value::HostValue("HostNumber", HostNumber{}));`

- `HostValueStorage` 把 C++ 对象放进 `std::any`：拷贝脚本 `Value` 即调用内含 C++ 类型的拷贝操作，local/global/参数/返回值获得独立存储；注册默认值用于默认构造，同类型单值构造提供拷贝构造，其他构造形式带位置诊断拒绝
- 注册值参与赋值、module 全局、按值传参/返回与 `out`/`inout` 回写；`SetArgValue` 可直接把值传给 prepared context；回调用 `AsHostValue<T>()` 检查参数
- const 方法经 portable 宿主方法桥可用，receiver 经 `GenericCall::GetObjectValue()` 暴露；**可变值类型方法刻意拒绝**——当前方法调用收到的是拷贝值，在编译器能写回 receiver 前允许修改会静默丢失变更；值类型属性同理暂不支持
- 差分用例在官方 2.38.0 注册构造/析构/拷贝构造/赋值/const 方法，两边跑同一脚本

## 相关文档

- [docs/stages/63-registered-value-types.md](../stages/63-registered-value-types.md)

## 涉及文件

```
 AGENTS.md                                    |  12 ++++---
 CMakeLists.txt                               |   7 +++++
 docs/stages/63-registered-value-types.md     |  42 +++++++++++++++++++++++++++
 include/mini_as/core.hpp                     |  28 ++++++++++++++++--
 include/mini_as/engine.hpp                   |   2 ++
 include/mini_as/generic.hpp                  |   5 +++-
 include/mini_as/object.hpp                   |   2 ++
 include/mini_as/type_checker.hpp             |   2 ++
 src/bytecode.cpp                             |  16 ++++++++--
 src/core.cpp                                 |  10 +++++--
 src/engine.cpp                               |  42 ++++++++++++++++++++++-----
 src/generic.cpp                              |   5 ++--
 src/type_checker.cpp                         |   8 +++++
 src/vm.cpp                                   |  21 ++++++++++----
 tests/compat/cases/registered_value_types.as |   5 +++
 tests/compat/mini_runner.cpp                 |  28 +++++++++++++-----
 tests/compat/official_runner.cpp             |  62 ++++++++++++++++++++++++++++++++++++++++---
 tests/test_bytecode.cpp                      |  32 ++++++++++++++++++++
 tests/test_objects.cpp                       | 113 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 19 files changed, 406 insertions(+), 36 deletions(-)
```
