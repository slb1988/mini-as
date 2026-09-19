# feat: add source locations diagnostics and core values

- Commit: 80eb83c033bbc4fe2317292184ae64452810094c
- Date: 2026-07-30 00:53:46 +0800
- Author: sunlaibing

## 变更内容

引入贯穿整个引擎的基础类型层，建立编译期与运行期分离的根基：

- `SourceLocation`：记录 section、offset、row、column，之后每个 token、AST 节点、指令行号和运行时异常都会携带它
- 诊断系统 `Diagnostic` / `DiagnosticSink`：诊断以数据形式收集，可同时流式转发给宿主回调；编译器代码永不直接写控制台
- `DataType`：编译期的类型知识，描述一个表达式「被允许做什么」，支持 Void/Bool/Int/Float/String/Object 以及 handle 标记
- `Value`：运行期的值存储。AngelScript 用 DWORD 大小的 slot 存放原始值，本教学实现改用带标签的 `std::variant`，以紧凑性换取透明与安全；后续 bytecode 仍是带类型的，编译期不变量同样得到检验

配套补充了针对 `DataType`/`Value` 行为的单元测试。

## 相关文档

- [docs/stages/01-core-values.md](../stages/01-core-values.md)

## 涉及文件

```
 CMakeLists.txt                |  6 ++-
 docs/stages/01-core-values.md | 15 +++++++
 include/mini_as/core.hpp      | 97 +++++++++++++++++++++++++++++++++++++++++++
 src/core.cpp                  | 97 +++++++++++++++++++++++++++++++++++++++++++
 tests/test_main.cpp           |  8 +++-
 5 files changed, 220 insertions(+), 3 deletions(-)
```
