# feat(host): support registered global properties

- Commit: 7d96588291cc811d1b4493c9feed004b70e32369
- Date: 2026-08-09 20:59:00 +0800
- Author: sunlaibing

## 变更内容

宿主可向脚本暴露活跃的原始类型/string 存储：`engine->RegisterGlobalProperty("int hostCounter", &counter);`

- 声明解析接受 `type name` 与 `const type name`；注册属性先于 module 全局进入类型检查，脚本走普通全局 lvalue 路径并获得同样的类型与 const 赋值诊断；脚本全局不能遮蔽同名注册属性
- 每个属性获得引擎稳定的 `GlobalId`；module image 把该 id 链接到引擎稳定注册表的条目，bytecode 绝不嵌入宿主指针；普通读写、复合赋值、`out`/`inout` 回写都经过共享的 VM 全局访问 helper——所有引用该属性的 context 与 module 读即时可见宿主修改、写立即更新宿主存储
- 宿主拥有被注册的 `Value` 并须保证其存活；注册时验证初始类型，VM 每次访问再验证一次——宿主误换成别的 `Value` 类型会变成带位置的脚本异常而非状态损坏
- 本阶段覆盖数值、bool 与内置 string；对象属性依赖后续注册对象类型工作；官方风格整数结果码留给 `mini_as::compat` 门面

## 相关文档

- [docs/stages/59-registered-global-properties.md](../stages/59-registered-global-properties.md)

## 涉及文件

```
 AGENTS.md                                          |   8 +++---
 CMakeLists.txt                                     |   7 ++++++
 docs/stages/59-registered-global-properties.md     |  36 ++++++++++++++++++++++++
 include/mini_as/bytecode.hpp                       |   2 ++
 include/mini_as/engine.hpp                         |   3 +++
 include/mini_as/generic.hpp                        |   7 +++++
 include/mini_as/type_checker.hpp                   |   3 +++
 src/engine.cpp                                     |  66 +++++++++++++++++++++++++++++++++++++++++++++-
 src/generic.cpp                                    |  29 +++++++++++++++++++
 src/type_checker.cpp                               |  11 ++++++--
 src/vm.cpp                                         |  52 +++++++++++++++++++++++-------------
 tests/compat/cases/registered_global_properties.as |   4 +++
 tests/compat/mini_runner.cpp                       |   9 ++++--
 tests/compat/official_runner.cpp                   |  11 ++++++--
 tests/test_bytecode.cpp                            |  34 +++++++++++++++++++++++
 tests/test_generic.cpp                             | 117 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 16 files changed, 369 insertions(+), 30 deletions(-)
```
