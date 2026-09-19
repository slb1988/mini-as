# feat(modules): support imported functions

- Commit: 8a0f40b232b32a490ff78698bc54eb9dfc0ef809
- Date: 2026-08-11 22:30:33 +0800
- Author: sunlaibing

## 变更内容

script module 可声明由其他 module 在编译后提供实现的函数：`import int Add(int value) from "math";`

- import 声明参与正常重载决议与 bytecode 生成，但 `Build` 不会隐式搜索或合并源 module——宿主显式调用 `BindImportedFunction`/`BindAllImportedFunctions`，匹配 AngelScript「编译与应用 controlled 链接分离」的模型
- 每个 import 记录带类型声明、稳定声明 `FunctionId` 与建议源 module 名；编译出的调用使用 `CallableKind::ImportedFunction`；可变绑定存放在 `ModuleState` 中（全局旁边），绑定/解绑不改动不可变 `ModuleImage`
- 执行时 VM 经引擎决议绑定目标：进入 import 函数会切换活跃 bytecode module 与全局状态并同时保留两个镜像；每个调用帧保存/恢复 bytecode 与 module 全局状态、不可变镜像所有权、新建对象用的 finalizer bytecode 与状态、普通 locals/捕获/operand-stack 基线/pc——支持嵌套与循环 module 调用而不混淆双方全局；异常保留失败位置与跨 module 调用栈；调用未绑定 import 报带位置的 `imported function is not bound`
- native module API 暴露 import 计数、声明与源 module 检查、单个/全部绑定与解绑；compat facade 提供对应官方风格结果码方法；绑定要求精确的全局函数 callable 形状（返回类型、参数、参数模式、引用限定匹配），手动绑定可如官方 API 选择不同名的脚本或注册宿主函数
- bytecode 格式升至 version 3：持久化 import 签名、源 module 名与 import 调用描述符；活绑定刻意排除——加载的 module 以未绑定开始，须由宿主重新链接

## 相关文档

- [docs/stages/81-imported-functions.md](../stages/81-imported-functions.md)

## 涉及文件

```
 AGENTS.md                                |  13 +++-
 CMakeLists.txt                           |   7 ++
 docs/stages/81-imported-functions.md     |  70 +++++++++++++++++
 include/mini_as/bytecode.hpp             |  13 +++-
 include/mini_as/compat.hpp               |   9 +++
 include/mini_as/engine.hpp               |   9 +++
 include/mini_as/parser.hpp               |   4 +-
 include/mini_as/tokenizer.hpp            |   2 +-
 include/mini_as/type_checker.hpp         |   2 +
 include/mini_as/vm.hpp                   |  31 +++++++-
 src/bytecode.cpp                         |  37 +++++++--
 src/bytecode_io.cpp                      |  28 +++++--
 src/compat.cpp                           |  36 +++++++++
 src/engine.cpp                           | 116 ++++++++++++++++++++++++++++-
 src/parser.cpp                           |  29 ++++++-
 src/tokenizer.cpp                        |   5 +-
 src/type_checker.cpp                     |  11 ++-
 src/vm.cpp                               |  87 +++++++++++++++++++--
 tests/compat/cases/imported_functions.as |   5 ++
 tests/compat/mini_runner.cpp             |   9 +++
 tests/compat/official_runner.cpp         |  14 ++++
 tests/test_compat.cpp                    |  25 +++++++
 tests/test_engine.cpp                    | 126 +++++++++++++++++++++++++++++++
 tests/test_parser.cpp                    |  18 +++++
 tests/test_tokenizer.cpp                 |  11 +++
 25 files changed, 684 insertions(+), 33 deletions(-)
```
