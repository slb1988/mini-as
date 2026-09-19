# feat(language): support initialization lists

- Commit: 3f343761cc7c86a977fc8ba4872fda2b8a12c260
- Date: 2026-08-11 21:32:29 +0800
- Author: sunlaibing

## 变更内容

表达式支持 AngelScript 风格花括号初始化列表（目标注册对象暴露容器协议时）：`array<int>@ values = {20, 21, 1};`、`array<int>@ empty = {};`

- 接受尾随逗号；元素类型由目标类型提供——没有上下文类型时花括号列表对 `auto` 刻意无效
- **基于协议的降级**：初始化列表是专门的 `InitList` AST 节点；类型检查要求目标是具备「零参 factory + 单元素参数 `insertLast(T)` 方法」的注册宿主类型，每个表达式按 `T` 检查与转换——该特性对将来的注册容器可复用，而非按名字识别 `array`
- bytecode 调用普通零参宿主 factory，为每个元素复制 handle 并调用普通 `insertLast`；VM 只看到既有 `Dup`/`CallHost`/`Pop` 指令，没有新增数组专用 opcode 或运行时分支
- 诊断：无类型列表报告需要目标对象类型；不满足协议的对象被拒绝；元素不兼容时在元素位置报告期望与实际类型；失败时保持候选镜像事务性

## 相关文档

- [docs/stages/76-initialization-lists.md](../stages/76-initialization-lists.md)

## 涉及文件

```
 AGENTS.md                                  |  5 ++-
 CMakeLists.txt                             |  7 ++++
 docs/stages/76-initialization-lists.md     | 44 ++++++++++++++++++++++++
 include/mini_as/bytecode.hpp               |  1 +
 include/mini_as/parser.hpp                 |  2 +-
 src/bytecode.cpp                           | 35 ++++++++++++++++++++
 src/bytecode_io.cpp                        |  2 +-
 src/parser.cpp                             |  9 +++++
 src/type_checker.cpp                       | 31 ++++++++++++++++++
 tests/compat/cases/initialization_lists.as |  5 +++
 tests/compat/mini_runner.cpp               |  3 +-
 tests/compat/official_runner.cpp           |  3 +-
 tests/test_addons.cpp                      | 64 ++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                      | 14 ++++++++
 14 files changed, 220 insertions(+), 5 deletions(-)
```
