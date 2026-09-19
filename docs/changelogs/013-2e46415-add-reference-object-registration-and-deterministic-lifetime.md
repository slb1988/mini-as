# feat: add reference object registration and deterministic lifetime

- Commit: 2e4641539279df9469e3377edd26355cbbbda07f
- Date: 2026-07-30 01:12:54 +0800
- Author: sunlaibing

## 变更内容

引入 reference object 注册与确定性生命周期：

- `RefObject` 是 portable 宿主协议：原子侵入式引用计数、稳定的 `TypeInfo`，以及为 GC 预留的虚函数引用枚举器
- `ObjectHandle` 是值对象：拷贝、移动、赋值、析构都被翻译为该协议要求的精确 AddRef/Release 操作
- 因为 `Value` 内嵌 `ObjectHandle`，同一套生命周期规则自动覆盖常量、locals、参数、返回值、operand stack 临时值与 `GenericCall`；在此简化 VM 中替换 local 或展开 context 即释放旧对象，无需专门的清理 opcode

测试验证各作用域边界的引用计数、对象在脚本与宿主调用间的往返、确定性析构，以及「注册类型与函数参数不符的对象在运行期被拒绝」。

## 相关文档

- [docs/stages/12-reference-objects.md](../stages/12-reference-objects.md)

## 涉及文件

```
 CMakeLists.txt                      |  2 ++
 docs/stages/12-reference-objects.md | 15 ++++++++
 include/mini_as/core.hpp            | 24 +++++++++++--
 include/mini_as/engine.hpp          |  5 +++
 include/mini_as/generic.hpp         |  3 +-
 include/mini_as/object.hpp          | 38 +++++++++++++++++++++
 src/core.cpp                        | 12 +++++--
 src/engine.cpp                      | 15 ++++++++
 src/generic.cpp                     |  3 +-
 src/object.cpp                      | 43 +++++++++++++++++++++++
 src/type_checker.cpp                |  1 -
 tests/test_objects.cpp              | 68 +++++++++++++++++++++++++++++++++++++
 12 files changed, 222 insertions(+), 7 deletions(-)
```
