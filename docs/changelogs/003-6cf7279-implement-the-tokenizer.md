# feat: implement the tokenizer

- Commit: 6cf727906eb451b8aa022169ccf21b6e770bdf9b
- Date: 2026-07-30 00:55:06 +0800
- Author: sunlaibing

## 变更内容

实现 tokenizer（词法分析器），对应 AngelScript 手写 tokenizer 的精简版（去掉了大字面量与可移植性包袱）。设计要点：

- 单次前向状态机：识别最长合法 operator（区分 `=` 与 `==`）；keyword 不作为独立扫描规则，而是 identifier 的后分类（post-classification）
- 在消费 token 之前记录 `SourceLocation`；comment 会被丢弃，但仍推进行、列计数
- 字符串扫描区分转义引号与结束引号；token 保留原始 lexeme，转义解码留给 parser/evaluator，保证诊断信息总能引用源码的确切拼写
- 词法错误不抛异常：只通过 `DiagnosticSink` 上报，并且始终保证最后发出一个 `End` token

token 类型集覆盖了本阶段所需的关键字（void/bool/int/float/string/true/false/if/else/while/return/class/interface/is/null）、分隔符与运算符。配套单元测试验证 token 序列与位置记录。

## 相关文档

- [docs/stages/02-tokenizer.md](../stages/02-tokenizer.md)

## 涉及文件

```
 CMakeLists.txt                |   1 +
 docs/stages/02-tokenizer.md   |  14 ++++
 include/mini_as/tokenizer.hpp |  59 ++++++++++++++++
 src/tokenizer.cpp             | 161 ++++++++++++++++++++++++++++++++++++++++++
 tests/test_main.cpp           |   7 ++
 5 files changed, 242 insertions(+)
```
