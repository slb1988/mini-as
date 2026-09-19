# refactor(compiler): add reusable constant expression evaluator

- Commit: 9df1e70fd531ed771cbba8316a24cadee3e10580
- Date: 2026-08-08 12:07:18 +0800
- Author: sunlaibing

## 变更内容

抽出可复用的 `ConstantExpressionEvaluator`（`constant_evaluator.hpp/.cpp`）：把原本散落在 `bytecode.cpp` 里的字面量求值（整数/浮点解析、字符串转义解码 `DecodeString`、布尔字面量）收敛为对 AST 表达式返回 `std::optional<Value>` 的独立组件，bytecode 编译器改为调用它。

此举为后续 const 变量求值、默认参数、case 标签、enum 值等「编译期常量」场景提供统一入口。附独立单元测试 `test_constant_evaluator.cpp`。

## 涉及文件

```
 CMakeLists.txt                         |   2 +
 include/mini_as/constant_evaluator.hpp |  14 +++++
 src/bytecode.cpp                       |  34 ++--------
 src/constant_evaluator.cpp             | 110 +++++++++++++++++++++++++++++++++
 tests/test_constant_evaluator.cpp      |  33 ++++++++++
 5 files changed, 163 insertions(+), 30 deletions(-)
```
