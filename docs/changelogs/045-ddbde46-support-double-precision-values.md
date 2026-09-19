# feat(types): support double precision values

- Commit: ddbde46748ec5fa43016e339af36ecc3472e9051
- Date: 2026-08-08 13:43:21 +0800
- Author: sunlaibing

## 变更内容

`double` 成为一等 `DataType` 与 `Value` 变体：

- 编译器在加宽整数/浮点时发射 `TO_DOUBLE`，窄化 double 时发射 `TO_FLOAT`；新增专用指令 `ADD_D`/`SUB_D`/`MUL_D`/`DIV_D`/`NEG_D`
- 混合数值表达式按 double → float → 整数提升规则选择类型；VM 的比较与算术保持在双精度内，除零在原指令处报告
- 现有 float API 不变；`ScriptContext` 新增 `SetArgDouble`/`GetReturnDouble`，`GenericCall` 新增对应的 double 访问器——portable 宿主桥无需 ABI 相关约定即可交换新值类型
- 整数常量已经可以精确初始化 double；无后缀小数字面量的行为保持现状（精确的进制与后缀类型控制属于下一阶段）

## 相关文档

- [docs/stages/33-double-precision.md](../stages/33-double-precision.md)

## 涉及文件

```
 CMakeLists.txt                         |  7 +++++++
 docs/stages/33-double-precision.md     | 17 +++++++++++++++++
 include/mini_as/bytecode.hpp           |  5 +++--
 include/mini_as/core.hpp               |  6 ++++--
 include/mini_as/engine.hpp             |  2 ++
 include/mini_as/generic.hpp            |  2 ++
 include/mini_as/tokenizer.hpp          |  2 +-
 src/bytecode.cpp                       | 51 ++++++++++++++++++++++++++++++--------------------
 src/constant_evaluator.cpp             | 27 ++++++++++++++++----------
 src/core.cpp                           | 18 ++++++++++++++++--
 src/engine.cpp                         | 11 +++++++++++
 src/generic.cpp                        |  3 +++
 src/interpreter.cpp                    | 13 +++++++------
 src/object.cpp                         |  1 +
 src/parser.cpp                         |  3 ++-
 src/tokenizer.cpp                      |  1 +
 src/type_checker.cpp                   |  8 +++++++-
 src/vm.cpp                             | 29 ++++++++++++++++++++++++++---
 tests/compat/cases/double_precision.as |  6 ++++++
 tests/test_bytecode.cpp                | 15 +++++++++++++++
 tests/test_engine.cpp                  | 37 +++++++++++++++++++++++++++++++++++++
 tests/test_generic.cpp                 | 15 +++++++++++++++
 22 files changed, 238 insertions(+), 41 deletions(-)
```
