# feat(bytecode): support versioned bytecode save and load

- Commit: f0053318d0f692b078b3609dc6e02fd2e925c155
- Date: 2026-08-11 13:09:38 +0800
- Author: sunlaibing

## 变更内容

module 可经标准 C++ 流持久化与恢复编译产物：`module->SaveBytecode(stream)` / `otherModule->LoadBytecode(stream)`。

- 二进制格式：`MASB` magic + 显式格式版本；整数定长小端；长度前缀的字符串与集合带防御性上限；负载有精确声明长度；枚举与 opcode 值校验；FNV-1a 校验和在任何 module 状态变更前拒绝损坏数据；v1 是内部格式，不声称与 AngelScript SDK bytecode 兼容
- 归档内容：带类型函数、指令、常量、异常处理器、调用描述符、虚分派、全局绑定与初始化 bytecode、析构链接、funcdef、完整编译环境、已移除函数 id、arena 持有的类型化定义树（恢复定义树使后续动态编译仍能物化旧默认参数表达式）
- 稳定 id 是引擎局部的，加载绝不把序列化数值 id 当作活身份：脚本类型/函数/全局获得目标 module 的新 id，每条指令、callable、分派项、析构器、常量函数 handle、引用、移除标记都被重映射；宿主函数/属性/对象/值类型/枚举/typedef/funcdef 必须匹配当前注册并重新绑定
- 加载是事务性的：先在候选状态完成解析、校验、重映射、链接与全局初始化执行，再发布反射元数据与新不可变镜像；bad magic、版本不支持、截断、长度错误、校验和失败、宿主注册缺失、全局初始化失败都保留旧镜像
- 与 AngelScript 的 bytecode API 一样，这里存的是编译产物而非活运行时对象图；活全局/对象/挂起 context 的序列化属于 v0.6 阶段

## 相关文档

- [docs/stages/70-versioned-bytecode-save-load.md](../stages/70-versioned-bytecode-save-load.md)

## 涉及文件

```
 AGENTS.md                                      |  13 +-
 CMakeLists.txt                                 |   1 +
 docs/stages/70-versioned-bytecode-save-load.md |  53 +++
 include/mini_as/engine.hpp                     |   3 +
 src/bytecode_io.cpp                            | 731 ++++++++++++++++++++++++++
 src/bytecode_io.hpp                            |  21 +
 src/engine.cpp                                 | 325 +++++++++++-
 tests/compat/mini_runner.cpp                   |  11 +-
 tests/compat/official_runner.cpp               |  40 ++
 tests/test_engine.cpp                          | 158 +++++-
 10 files changed, 1350 insertions(+), 6 deletions(-)
```
