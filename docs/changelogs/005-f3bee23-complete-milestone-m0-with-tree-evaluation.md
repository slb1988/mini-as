# feat: complete milestone m0 with tree evaluation

- Commit: f3bee23bea92c4a2095661c80e79de3715857f6d
- Date: 2026-07-30 00:58:08 +0800
- Author: sunlaibing

## 变更内容

完成里程碑 M0：用 `Print(42);` 打通完整的前端管线——tokenize → parse → 遍历表达式树 → 跨入宿主回调。host function 接收 `Value` 数组并返回 `Value`，这是 AngelScript portable generic calling convention 的最小形态。

tree-walking 求值是刻意的临时方案：每个 AST 节点直接触发其语义操作，便于观察调试；但 C++ 递归让 suspension 和脚本可见的调用栈难以实现，后续 bytecode 阶段会替换它，同时保留同一套 token、AST、Value 与诊断体系。附 `examples/m0` 示例。

## 相关文档

- [docs/stages/04-m0-tree-evaluator.md](../stages/04-m0-tree-evaluator.md)

## 涉及文件

```
 CMakeLists.txt                      |   4 +
 docs/stages/04-m0-tree-evaluator.md |  11 +++
 examples/m0/main.cpp                |  18 +++++
 include/mini_as/interpreter.hpp     |  35 ++++++++
 src/interpreter.cpp                 | 157 ++++++++++++++++++++++++++++++++++++
 tests/test_main.cpp                 |   9 +++
 6 files changed, 234 insertions(+)
```
