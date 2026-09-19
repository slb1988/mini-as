# feat(module): support dynamic function compilation

- Commit: 47ad647281cb98f842062a2985038b5a86db0e16
- Date: 2026-08-11 12:39:55 +0800
- Author: sunlaibing

## 变更内容

`ScriptModule::CompileFunction` 支持在不重建原始脚本 section 的情况下增量编译单个全局函数：

- 新函数针对 module 完整成功构建环境编译（全局函数/变量、类与接口、枚举、typedef、funcdef、宿主注册）；`addToModule=true` 时对查找与后续增量编译可见，重复声明被拒绝且不改动当前镜像
- 成功镜像保留 arena 持有的类型检查定义树：新调用省略实参时，既有函数的默认参数表达式仍按其原始 namespace 在新调用点物化
- `addToModule=false` 产生 detached 函数：返回的裸指针在 module 生命周期内可执行，但不经 `GetFunctionByDecl` 可见，也不能递归引用自身——对应 AngelScript 编译标志 `0` 与 `asCOMP_ADD_TO_MODULE` 的区别；可选 `lineOffset` 调整诊断与运行时行号
- 每次成功编译产生新的不可变 `ModuleImage`，新旧镜像共享既有 `ModuleState`（全局保持身份与值）；编译前 prepared 的 context 继续持有旧镜像并安全完成；所有动态镜像被 module 保留，早先增量编译返回的裸指针在后续编译后仍有效，并注册进引擎使 `Prepare` 捕获其所有权
- callable 描述符表精心重建：旧描述符保持原索引供既有 bytecode 使用，新函数指令重定基到其后追加的描述符——保证既有函数、新函数、闭包、delegate、宿主调用与虚调用共存于一个镜像

动态移除留给下一独立提交（089）。

## 相关文档

- [docs/stages/68-dynamic-function-compilation.md](../stages/68-dynamic-function-compilation.md)

## 涉及文件

```
 AGENTS.md                                      |  15 ++-
 docs/stages/68-dynamic-function-compilation.md |  50 ++++++++
 include/mini_as/bytecode.hpp                   |   3 +-
 include/mini_as/engine.hpp                     |  15 +++
 src/bytecode.cpp                               |  72 ++++++-----
 src/engine.cpp                                 | 202 +++++++++++++++++++++++++++
 tests/compat/mini_runner.cpp                   |   6 +
 tests/compat/official_runner.cpp               |  15 +++
 tests/test_engine.cpp                          | 104 +++++++++++++++
 9 files changed, 448 insertions(+), 34 deletions(-)
```
