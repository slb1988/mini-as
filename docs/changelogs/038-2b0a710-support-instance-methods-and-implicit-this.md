# feat(classes): support instance methods and implicit this

- Commit: 2b0a710168d3139a8978ecdd3e2b3afb37f9df54
- Date: 2026-08-08 12:56:55 +0800
- Author: sunlaibing

## 变更内容

class 方法体进入类型检查与编译，支持隐式 `this`：

- 方法就是一个普通 bytecode function：有稳定 `FunctionId`、所属 `TypeId`，以及隐藏的 0 号 local slot 存放 receiver，显式参数排在其后
- 方法体内未决议的 identifier 回退到当前类的字段；不加限定的方法调用自动加载隐藏的 receiver
- 显式 `object.method(args)` 先求值 receiver 再求值参数，使用 stage 025 引入的 `ScriptMethod` callable 描述符；VM 构建被调方 local frame 时把 receiver 一并纳入，完全复用现有 call/return 机制

## 相关文档

- [docs/stages/28-instance-methods.md](../stages/28-instance-methods.md)

## 涉及文件

```
 CMakeLists.txt                         |   7 +++
 docs/stages/28-instance-methods.md     |  11 ++++
 include/mini_as/bytecode.hpp           |   4 +-
 include/mini_as/parser.hpp             |   1 +
 include/mini_as/type_checker.hpp       |   5 ++
 src/bytecode.cpp                       | 105 +++++++++++++++++++++++++++------
 src/engine.cpp                         |  10 +++-
 src/type_checker.cpp                   |  65 +++++++++++++++++++---
 src/vm.cpp                             |   3 +-
 tests/compat/cases/instance_methods.as |  12 ++++
 tests/test_engine.cpp                  |  29 +++++++++
 11 files changed, 222 insertions(+), 30 deletions(-)
```
