# feat(functions): support variadic arguments

- Commit: 522859eb49198806efac4da9d8da696860bdb8f6
- Date: 2026-08-12 00:39:52 +0800
- Author: sunlaibing

## 变更内容

注册泛型函数对齐 AngelScript 2.38.0 的变参调用模型：声明的最后一个参数是重复原型，如 `int sum(int seed, int ...)`、`int formatCount(const string &in format, const ? &in ...)`、`void scan(const string &in input, ? &out ...)`。

- 变参段至少接收一个实参（与 2.38.0 一致）；仅限注册 `GenericCall` 回调——脚本函数不能声明 `...`，mini 仍无原生 ABI 调用约定
- **编译模型**：`FunctionSignature::variadic` 把末参数标记为原型而非固定槽；实参排序保持固定前缀并追加各位置变参表达式；命名实参只能指向固定前缀；类型检查对尾部重复原型的类型与引用模式，可行非变参重载优先于变参重载
- 通配符 `?` 只存在于注册参数声明：接受官方 `const ? &in ...` 与 `? &out ...`，拒绝 `? &inout ...`、裸 `?` 与非尾部省略号；固定类型变参可用值或引用参数
- 每个 bytecode `CallableRef` 记录调用点实际参数个数——同一注册函数的两次调用现在可能弹出不同栈形状；bytecode 格式升至 version 7 持久化变参标记，加载仍按稳定声明重绑定宿主回调
- **运行时桥**：`GenericCall::GetArgCount()` 报告固定+变参总数，`GetArgType(index)` 暴露具体 `DataType`；通配符输入保留原始 `Value` 表示；通配符输出槽以目标 lvalue 实际类型初始化，VM 在回写前验证回调写入类型一致；注册全局函数、引用类型 factory、对象方法与宿主函数 handle 共享该路径

## 相关文档

- [docs/stages/91-variadic-arguments.md](../stages/91-variadic-arguments.md)

## 涉及文件

```
 AGENTS.md                                |   7 ++
 CMakeLists.txt                           |   7 ++
 docs/stages/91-variadic-arguments.md     |  54 +++++++++
 include/mini_as/bytecode.hpp             |   7 +-
 include/mini_as/core.hpp                 |   3 +-
 include/mini_as/generic.hpp              |   5 +-
 include/mini_as/type_checker.hpp         |   1 +
 src/bytecode.cpp                         | 115 +++++++++++++-----
 src/bytecode_io.cpp                      |  18 ++-
 src/core.cpp                             |   2 +
 src/engine.cpp                           |   9 +-
 src/generic.cpp                          |  56 ++++++++-
 src/type_checker.cpp                     |  98 +++++++++------
 src/vm.cpp                               |  71 ++++++++---
 tests/compat/cases/variadic_arguments.as |   3 +
 tests/compat/mini_runner.cpp             |   9 ++
 tests/compat/official_runner.cpp         |  16 +++
 tests/test_generic.cpp                   | 197 +++++++++++++++++++++++++++++++
 tests/test_objects.cpp                   |  55 +++++++++
 19 files changed, 637 insertions(+), 96 deletions(-)
```
