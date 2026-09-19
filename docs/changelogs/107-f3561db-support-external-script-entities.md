# feat(modules): support external script entities

- Commit: f3561dba37630c604dd6e8dce21b0a5ae6930df8
- Date: 2026-08-11 23:00:05 +0800
- Author: sunlaibing

## 变更内容

module 可引用已由其他 module 编译的共享类/接口/枚举/funcdef/全局函数，而无需重复其实现：

```angelscript
external shared class Counter;
external shared interface ICounter;
external shared int Twice(int value);
```

- `shared external` 顺序同样接受；两个词都保持上下文标识符；external 声明必须是 `shared`、以分号结尾、且匹配同一引擎此前发布的实体
- 成功的 shared 构建发布规范签名并保留属主定义树；后续 external 构建只把请求的规范签名注入类型检查——保留的树提供构造器默认参数与字段 initializer，但函数体不拷入消费方 module
- external 调用使用 `CallableKind::ExternalFunction` 与共享实体的引擎级稳定 `FunctionId`；运行时既有脚本函数决议器选出定义方的不可变 `ModuleImage`（含其全局状态、所有权、finalizer 状态与源码位置）；虚调用与析构器在实现不在消费方 bytecode 时使用同一决议回退
- 无需 import 绑定步骤；缺失或不匹配的先前定义是编译期错误，而陈旧运行环境中实现不可用是带位置运行时异常
- bytecode 格式升至 version 5：持久化签名与 AST 节点上的 external 标记，接受 external callable 描述符；加载定义方 module 会重建引擎规范共享注册表，加载的消费方继续按稳定 ID 决议 external 调用

## 相关文档

- [docs/stages/83-external-shared-entities.md](../stages/83-external-shared-entities.md)

## 涉及文件

```
 AGENTS.md                                  |  11 ++-
 CMakeLists.txt                             |   7 ++
 docs/stages/83-external-shared-entities.md |  48 ++++++++++
 include/mini_as/bytecode.hpp               |   5 +-
 include/mini_as/engine.hpp                 |   5 +
 include/mini_as/parser.hpp                 |   2 +
 include/mini_as/type_checker.hpp           |   1 +
 src/bytecode.cpp                           |  33 +++++--
 src/bytecode_io.cpp                        |  10 +-
 src/engine.cpp                             | 144 ++++++++++++++++++++++++++---
 src/parser.cpp                             |  37 +++++++-
 src/type_checker.cpp                       |  49 ++++++++++
 src/vm.cpp                                 |  37 +++++++-
 tests/compat/cases/external_entities.as    |   9 ++
 tests/compat/mini_runner.cpp               |   4 +-
 tests/compat/official_runner.cpp           |   4 +-
 tests/test_engine.cpp                      |  85 +++++++++++++++++
 tests/test_parser.cpp                      |  23 +++++
 18 files changed, 482 insertions(+), 32 deletions(-)
```
