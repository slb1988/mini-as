# feat(host): support namespaces access masks and configuration groups

- Commit: d92b9c9ff77b49a664afced6eb965daf7eb8966e
- Date: 2026-08-11 16:04:49 +0800
- Author: sunlaibing

## 变更内容

对齐 AngelScript 2.38.0 剩余的宿主注册控制，native API 与 compat facade 同步暴露：

- **默认 namespace**：`ScriptEngine::SetDefaultNamespace` 限定后续注册的全局函数、属性、对象类型、枚举、typedef、funcdef；factory/方法/属性的属主与引用类型按活跃宿主 namespace 决议；`ScriptModule::SetDefaultNamespace` 影响函数查找与动态函数编译（动态函数在该 namespace 中解析，普通 section 保留源码中的显式 namespace）；名字按 `identifier(::identifier)*` 校验，空串恢复全局
- **access mask**：每次宿主注册捕获引擎当前 32 位默认掩码；module 创建时捕获该默认值且可用 `SetAccessMask` 替换；只有掩码相交的注册被注入 module 的解析/检查/编译环境——被隐藏的符号以普通「未知声明」失败，而不是拖到运行期 VM 绑定失败
- **configuration group**：`BeginConfigGroup`/`EndConfigGroup`（不可嵌套）框住一组注册；`RemoveConfigGroup` 先扫描存活 module 环境，有 module 依赖时拒绝移除；成功移除把注册标记为 inactive 而非从指针稳定的 deque 中抹除（bytecode 镜像与 prepared context 可能仍持有其他组的回调描述符）；inactive 注册被排除在未来构建、动态编译、反射查找与 bytecode 宿主重绑定之外

## 相关文档

- [docs/stages/73-host-registration-controls.md](../stages/73-host-registration-controls.md)

## 涉及文件

```
 AGENTS.md                                    |  14 +-
 CMakeLists.txt                               |   7 +
 docs/stages/73-host-registration-controls.md |  63 +++++++
 include/mini_as/compat.hpp                   |  10 ++
 include/mini_as/engine.hpp                   |  33 +++-
 include/mini_as/generic.hpp                  |   9 +
 include/mini_as/object.hpp                   |   3 +
 src/compat.cpp                               |  28 +++
 src/engine.cpp                               | 413 +++++++++++++++++++++++++++++++++++++++----
 tests/compat/cases/host_controls.as          |   3 +
 tests/compat/mini_runner.cpp                 |  16 ++
 tests/compat/official_runner.cpp             |  29 +++
 tests/test_compat.cpp                        |  62 ++++++
 13 files changed, 621 insertions(+), 69 deletions(-)
```
