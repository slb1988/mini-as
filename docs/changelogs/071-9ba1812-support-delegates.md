# feat(functions): support delegates

- Commit: 9ba18125249e5d9d53f9af6c3fd004c18d8e3bd8
- Date: 2026-08-09 18:24:01 +0800
- Author: sunlaibing

## 变更内容

支持 delegate：`Callback(object.method)` 把对象实例与其一个方法绑定为 funcdef handle（stage 52 的同一 handle 类型），可存储、传递、重赋值、比较并经 `CallHandle` 调用。

- 类型检查要求与 funcdef 精确匹配（返回类型、返回引用限定、参数类型与模式）；重载方法按签名选择，适用正常成员访问规则
- `MakeDelegate` 只求值一次 receiver，打包 funcdef 稳定 `TypeId` 与虚调用描述符（静态 receiver 类型 + 稳定虚槽，而非可移动方法指针）；调用时 VM 按 receiver 真实 `TypeInfo` 决议实现——从基类或接口 handle 构建的 delegate 仍调用具体对象的 override
- 内嵌 `ObjectHandle` 是强引用（与官方 delegate 生命周期一致）：原局部 handle 离开作用域后 delegate 仍可调用；delegate receiver 参与引用枚举与清除，既有收集器可回收「对象字段存了指向自身 delegate」这类环；从 null receiver 构建会抛带位置异常
- 本阶段绑定脚本实例方法；注册对象方法属 v0.5 宿主注册特性；弱绑定回调留给 weak reference 阶段

## 相关文档

- [docs/stages/53-delegates.md](../stages/53-delegates.md)

## 涉及文件

```
 AGENTS.md                       |  4 ++--
 CMakeLists.txt                  |  7 +++++++
 docs/stages/53-delegates.md     | 37 +++++++++++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp    | 10 +++++++++-
 include/mini_as/core.hpp        | 14 +++++++++++++-
 include/mini_as/parser.hpp      |  1 +
 src/bytecode.cpp                | 52 ++++++++++++++++++++++++++++++++++++++++++++++++++--
 src/core.cpp                    |  4 +++-
 src/object.cpp                  | 14 +++++++++++---
 src/type_checker.cpp            | 27 +++++++++++++++++++++++++++
 src/vm.cpp                      | 26 +++++++++++++++++++++++++-
 tests/compat/cases/delegates.as | 20 ++++++++++++++++++++
 tests/test_engine.cpp           | 78 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_gc.cpp               | 15 +++++++++++++++
 tests/test_types.cpp            | 17 +++++++++++++++++
 15 files changed, 314 insertions(+), 12 deletions(-)
```
