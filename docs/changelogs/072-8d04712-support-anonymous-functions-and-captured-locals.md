# feat(functions): support anonymous functions and captured locals

- Commit: 8d0471272971336e17ec408965b71d4475bb0857
- Date: 2026-08-09 18:45:14 +0800
- Author: sunlaibing

## 变更内容

支持 AngelScript 匿名函数语法创建 funcdef handle：`Binary@ operation = function(left, right) { return left + right; };`

- 目标 funcdef 提供省略的参数类型与模式；显式标注参数类型可在多个匹配 funcdef 间消歧；lambda 体按嵌套函数做类型检查，遵守 funcdef 的返回与引用契约；缺失匹配与推断歧义是编译错误
- 每个匿名函数降级为带稳定 `FunctionId` 的隐藏 bytecode 函数；`MakeClosure` 创建普通带类型 `FunctionHandle`，调用、赋值、传参、返回、异常展开与镜像生命周期全部复用 stage 52 机制；隐藏名源自源码 section 与位置，未变化的重建中保持稳定
- **捕获局部变量（文档化扩展）**：官方 2.38.0 支持匿名函数但明确禁止访问外层 local，mini 按路线图将其作为扩展实现——被引用的外层 local 惰性提升为共享可变 cell，创建作用域、多个闭包、嵌套闭包共享同一 cell，更新对所有持有者可见，且原栈帧返回后 cell 仍保活；module 全局继续走全局槽位，不拷入闭包
- 捕获的对象 handle 经闭包环境参与引用枚举：对象存了捕获自身的闭包所形成的环可被既有收集器看到并清除；暂不支持捕获隐式 `this`（可改为捕获显式局部对象 handle）

## 相关文档

- [docs/stages/54-anonymous-functions.md](../stages/54-anonymous-functions.md)

## 涉及文件

```
 AGENTS.md                                 |   4 ++--
 CMakeLists.txt                            |   7 +++++++
 docs/stages/54-anonymous-functions.md     |  41 +++++++++++++++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp              |  10 ++++++++--
 include/mini_as/core.hpp                  |  16 ++++++++++++++--
 include/mini_as/parser.hpp                |   5 ++++-
 include/mini_as/tokenizer.hpp             |   2 +-
 include/mini_as/type_checker.hpp          |   2 ++
 include/mini_as/vm.hpp                    |   8 ++++++++
 src/bytecode.cpp                          | 113 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-
 src/core.cpp                              |  12 ++++++++++--
 src/object.cpp                            |  30 ++++++++++++++++++++++++------
 src/parser.cpp                            |  36 ++++++++++++++++++++++++++++++++++++
 src/tokenizer.cpp                         |   3 ++-
 src/type_checker.cpp                      | 102 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-
 src/vm.cpp                                |  77 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++--
 tests/compat/cases/anonymous_functions.as |  11 +++++++++++
 tests/test_engine.cpp                     |  87 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_gc.cpp                         |  16 ++++++++++++++++
 tests/test_parser.cpp                     |  18 ++++++++++++++++++
 tests/test_tokenizer.cpp                  |   8 ++++++++
 tests/test_types.cpp                      |  17 +++++++++++++++++
 22 files changed, 596 insertions(+), 29 deletions(-)
```
