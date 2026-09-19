# fix(module): preserve last successful image on failed rebuild

- Commit: 3d986f4451d0ac24b69347cba422efc09ce87b9c
- Date: 2026-08-08 11:59:45 +0800
- Author: sunlaibing

## 变更内容

修复技术分享报告中指出的生命周期缺陷：此前 `Build()` 失败会把 `bytecode_` 清空，破坏「原子替换」的承诺，且已 prepare 的 context 会因 module 重建而悬垂。

修复方案：

- 引入 `ModuleImage`（包裹 `BytecodeModule`），`ScriptModule` 持有 `shared_ptr<const ModuleImage>`；构建只在成功时替换镜像，失败保留上一次成功的版本
- `ScriptContext` 同时持有镜像的 `shared_ptr`，执行期间旧镜像保持存活；engine 通过 `weak_ptr` 登记表（`RegisterModuleImage`/`FindModuleImage`）按函数指针反查镜像

补充了模块重建失败保留旧镜像、旧 context 继续执行的回归测试。

## 涉及文件

```
 include/mini_as/engine.hpp | 11 +++++++++-
 src/engine.cpp             | 53 +++++++++++++++++++++++++++++++++++-----------
 tests/test_engine.cpp      | 35 ++++++++++++++++++++++++++++++
 3 files changed, 86 insertions(+), 13 deletions(-)
```
