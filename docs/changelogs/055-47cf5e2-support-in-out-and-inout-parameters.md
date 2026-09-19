# feat(functions): support in out and inout parameters

- Commit: 47cf5e2a297b237c2d270d6c3fc16a0e89d23e20
- Date: 2026-08-09 09:23:55 +0800
- Author: sunlaibing

## 变更内容

参数支持 AngelScript 的 `&in`/`&out`/`&inout` 方向限定（裸 `&` 视为 `&inout`）。参数模式是持久 `FunctionSignature` 元数据，参与声明字符串、bytecode 查找、虚调用与宿主注册。

教学 VM 不把引用放进 `Value`，调用采用 copy-in/copy-out：

1. `in`/`inout` 把调用方当前值拷入被调方；`out` 以类型默认值起始
2. 被调方使用普通带类型 local slot 执行；`in` slot 在类型检查期只读
3. 正常返回后，被改动的 `out`/`inout` slot 在返回值之后发布，调用方通过自己的 `LValueRef` 写回

支持 local、module 全局与对象字段目标；输出目标必须是精确声明类型的可变 lvalue；运行时异常跳过 copy-out，被调方部分修改不会泄漏给调用方。`GenericCall` 暴露对应的 `SetArg*`，宿主回调通过替换参数槽实现输出参数，VM 执行与脚本函数相同的 copy-out 序列。差分 runner 为官方引擎开启了 `asEP_ALLOW_UNSAFE_REFERENCES`（官方对原始类型 `&inout` 的要求）。

## 相关文档

- [docs/stages/42-reference-parameters.md](../stages/42-reference-parameters.md)

## 涉及文件

```
 CMakeLists.txt                             |   7 +++++
 docs/stages/42-reference-parameters.md     |  27 ++++++++++++++++++++
 include/mini_as/bytecode.hpp               |   9 +++++++
 include/mini_as/generic.hpp                |  11 ++++++--
 include/mini_as/parser.hpp                 |   3 +++
 include/mini_as/tokenizer.hpp              |   3 ++-
 include/mini_as/type_checker.hpp           |   4 +++
 src/bytecode.cpp                           | 143 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-----
 src/engine.cpp                             |   3 ++-
 src/generic.cpp                            |  32 ++++++++++++++++++++--
 src/parser.cpp                             |   8 ++++++
 src/tokenizer.cpp                          |   4 +--
 src/type_checker.cpp                       |  77 +++++++++++++++++++++++++++++++++++++++++++++++----
 src/vm.cpp                                 |  32 ++++++++++++++++++++++
 tests/compat/cases/reference_parameters.as |  11 ++++++++
 tests/compat/official_runner.cpp           |   1 +
 tests/test_bytecode.cpp                    |  22 +++++++++++++++
 tests/test_engine.cpp                      |  74 ++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_generic.cpp                     |  19 ++++++++++++
 tests/test_parser.cpp                      |  15 ++++++++++
 20 files changed, 476 insertions(+), 29 deletions(-)
```
