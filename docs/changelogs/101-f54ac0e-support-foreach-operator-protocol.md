# feat(language): support foreach operator protocol

- Commit: f54ac0edd91fdf9d20a7e16a7229a094b17d3e96
- Date: 2026-08-11 21:54:00 +0800
- Author: sunlaibing

## 变更内容

`foreach` 通过 AngelScript 2.38.0 的迭代器运算符协议消费对象，而非特判数组：`foreach (auto value, auto index : values) { ... }`

- tokenizer 保留 `foreach` 关键字；parser 记录一个或多个带类型/`auto` 迭代变量 + range 表达式 + 循环体；类型检查从各值运算符推断 `auto` 变量
- **运算符契约**：range 必须提供精确签名——`opForBegin()`、`opForEnd(Iterator)`、`opForNext(Iterator)`、单值用 `opForValue(Iterator)` 或多值用编号的 `opForValue0/1/...`（单值未编号形式优先，匹配官方编译器）；返回值向显式类型的迭代变量转换；缺失/类型错误/不可达的运算符是编译错误；脚本方法与注册宿主方法走普通 callable 分派（虚方法保持虚）
- array add-on 注册官方五方法形态：无符号索引迭代器、`opForValue0` 为元素、`opForValue1` 为索引
- **降级**：range 表达式只求值一次，range 与迭代器存入隐藏 local；每轮等价于 `for (auto it = range.opForBegin(); !range.opForEnd(it); it = range.opForNext(it)) { auto value = range.opForValue0(it); ... }`；`continue` 指向 `opForNext` 调用，`break` 直接退出；无需 foreach 专用 opcode

## 相关文档

- [docs/stages/78-foreach-operator-protocol.md](../stages/78-foreach-operator-protocol.md)

## 涉及文件

```
 AGENTS.md                                       |   7 +-
 CMakeLists.txt                                  |   7 ++
 docs/stages/78-foreach-operator-protocol.md     |  64 +++++++++++
 include/mini_as/parser.hpp                      |   3 +-
 include/mini_as/tokenizer.hpp                   |   3 +-
 src/bytecode.cpp                                | 142 ++++++++++++++++++++++++
 src/bytecode_io.cpp                             |   4 +-
 src/parser.cpp                                  |  26 ++++-
 src/script_array.cpp                            |  23 ++++
 src/tokenizer.cpp                               |   7 +-
 src/type_checker.cpp                            |  88 +++++++++++++++
 tests/compat/cases/foreach_operator_protocol.as |  14 +++
 tests/compat/mini_runner.cpp                    |   3 +-
 tests/compat/official_runner.cpp                |   3 +-
 tests/test_addons.cpp                           | 102 +++++++++++++++++
 tests/test_parser.cpp                           |  20 ++++
 tests/test_tokenizer.cpp                        |  10 ++
 17 files changed, 514 insertions(+), 12 deletions(-)
```
