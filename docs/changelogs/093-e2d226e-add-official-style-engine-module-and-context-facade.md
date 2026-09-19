# feat(compat): add official style engine module and context facade

- Commit: e2d226ec4c207c221f7ae675ccd5c4d065f0bf43
- Date: 2026-08-11 13:58:09 +0800
- Author: sunlaibing

## 变更内容

新增 `include/mini_as/compat.hpp` 与 `mini_as::compat` 命名空间：官方风格 facade，让嵌入代码使用 AngelScript 形态的整数结果码、module 标志、编译标志与执行状态，而不改变既有 RAII 风格的 `mini_as` API。

- 数值常量取自 2.38.0 `angelscript.h`（成功=0、无效参数=-5、执行完成=0、挂起=1、always-create=2 等）
- 所有权刻意保持显式 C++：`CreateScriptEngine()` 返回持有者、engine 拥有 module 包装、`CreateContext` 返回 `unique_ptr`——不模拟官方 SDK 的 `AddRef`/`Release`、ABI、头文件、原生调用约定或接口 vtable；三个包装都提供 `Native()` 以便取用 facade 尚未覆盖的 mini 特性
- 初始表面：engine 提供消息回调、generic 全局函数/属性注册、enum/typedef/funcdef 注册、module 查找标志与 context 创建；module 提供官方形态的 section 添加、build、函数查找、带 `asCOMP_ADD_TO_MODULE` 的动态编译、移除、bytecode save/load（`lineOffset` 用前缀逻辑空行实现）；context 提供返回整数的 prepare/参数/执行、官方执行状态数、原始类型返回访问器、异常文本、suspend/abort、line callback 与栈/函数/行号/局部调试查询
- 包装层校验把 null 函数、非法声明、非法标志、参数越界、不可用栈层映射到最接近的 2.38.0 结果码

## 相关文档

- [docs/stages/72-official-style-compat-facade.md](../stages/72-official-style-compat-facade.md)

## 涉及文件

```
 AGENTS.md                                      |  12 +-
 CMakeLists.txt                                 |   9 ++
 docs/stages/72-official-style-compat-facade.md |  68 ++++++++++
 include/mini_as/compat.hpp                     | 154 ++++++++++++++++++++++++
 src/compat.cpp                                 | 222 ++++++++++++++++++++++++++++++++++
 tests/compat/cases/compat_facade.as            |   7 ++
 tests/compat/mini_runner.cpp                   |  15 +++
 tests/test_compat.cpp                          | 107 ++++++++++++++++
 8 files changed, 590 insertions(+), 4 deletions(-)
```
