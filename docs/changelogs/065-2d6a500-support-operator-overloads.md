# feat(classes): support operator overloads

- Commit: 2d6a5001bbbe863536b11f65a227f84b9b384eca
- Date: 2026-08-09 16:47:40 +0800
- Author: sunlaibing

## 变更内容

script class 支持 AngelScript 的核心运算符方法族：

- 算术/位运算/移位/幂表达式选择 `opAdd` … `opUShr`，对象在右侧时考虑对应 `_r` 方法；一元求负与取反用 `opNeg`/`opCom`；前后缀自增自减用四个专用方法；赋值覆盖 `opAssign` 与全部复合赋值变体
- 相等性先找返回 bool 的 `opEquals`，回退到返回 int 的 `opCmp`；有序比较也用 `opCmp`（含对象在右操作数时的反向比较规则）；`is` 运算符刻意不可重载，始终比较 handle 身份；可调用对象把 `value(args)` 降级为 `value.opCall(args)`
- 显式 `cast<T>` 可用 `opCast`/`opImplCast`；`int(value)` 等原始式转换与 `Label(value)` 类形式转换按返回类型选择 `opConv`/`opImplConv`——转换方法是唯一允许仅按返回类型重载的方法族；隐式转换在声明、赋值、参数、返回等常见转换点生效；bytecode 编译器消费已求值的 receiver 并发射一次虚调用，转换表达式不会被求值两次
- 所有运算符调用使用稳定虚布局与 `CallableRef`；bytecode 只含稳定 `TypeId` 与虚槽数据；null receiver 在运算符表达式位置报告
- 差分用例覆盖正/反向算术、`opEquals`/`opCmp`、身份比较、赋值、自增、`opCall`、显式与隐式转换；`opIndex` 留给索引阶段；脚本类实例仍是引用计数 handle，尚未实现官方完整值对象拷贝模型

## 相关文档

- [docs/stages/48-operator-overloads.md](../stages/48-operator-overloads.md)

## 涉及文件

```
 AGENTS.md                                |   6 +++---
 CMakeLists.txt                           |   7 +++++++
 docs/stages/48-operator-overloads.md     |  37 +++++++++++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp             |   3 +++
 include/mini_as/parser.hpp               |   4 +++-
 include/mini_as/type_checker.hpp         |   3 +++
 src/bytecode.cpp                         | 152 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-
 src/core.cpp                             |  20 ++++++++++++++++----
 src/parser.cpp                           |   9 +++++++++
 src/type_checker.cpp                     | 241 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-
 tests/compat/cases/operator_overloads.as |  56 +++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_bytecode.cpp                  |  27 +++++++++++++++++++++++++++
 tests/test_engine.cpp                    | 153 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 13 files changed, 699 insertions(+), 19 deletions(-)
```
