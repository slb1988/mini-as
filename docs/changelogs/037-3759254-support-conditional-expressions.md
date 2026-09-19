# feat(expressions): support conditional expressions

- Commit: 3759254b8d67fe3d40eaafd5d31158cdf08208a0
- Date: 2026-08-08 12:49:00 +0800
- Author: sunlaibing

## 变更内容

支持条件（三元）表达式 `condition ? whenTrue : whenFalse`：

- 右结合；条件必须是 bool；两个分支类型必须匹配或存在唯一无歧义的隐式转换——当前数值子集在需要时把 `int` 提升为 `float`
- bytecode 先求值条件再跳过未选中的分支：副作用与运行时错误只发生在被选中的表达式里；两条路径都在 operand stack 上留下一个推断结果类型的值
- 常量表达式求值器遵循同样的短路规则

## 相关文档

- [docs/stages/27-conditional-expressions.md](../stages/27-conditional-expressions.md)

## 涉及文件

```
 CMakeLists.txt                                |  7 +++++++
 docs/stages/27-conditional-expressions.md     | 10 ++++++++++
 include/mini_as/parser.hpp                    |  3 ++-
 include/mini_as/tokenizer.hpp                 |  2 +-
 src/bytecode.cpp                              | 15 +++++++++++++++
 src/constant_evaluator.cpp                    |  8 ++++++++
 src/parser.cpp                                | 13 ++++++++++++-
 src/tokenizer.cpp                             |  3 ++-
 src/type_checker.cpp                          | 12 ++++++++++++
 tests/compat/cases/conditional_expressions.as |  6 ++++++
 tests/test_engine.cpp                         | 35 +++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                         | 11 +++++++++++
 12 files changed, 121 insertions(+), 4 deletions(-)
```
