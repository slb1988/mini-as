# feat: separate engine module and context lifecycles

- Commit: 4c3a44ddde38f5edbae45d3a2b988c19f1bad36e
- Date: 2026-07-30 01:06:48 +0800
- Author: sunlaibing

## 变更内容

公开 API 拆分为三条生命周期，对齐 AngelScript 的对象模型：

- `ScriptEngine`：持有已注册的配置与命名 module
- `ScriptModule`：收集源码 section，只有在构建成功后才原子地替换 bytecode（构建失败不破坏旧镜像）
- `ScriptContext`：持有可变执行状态，可针对同一个不可变函数创建多次

多个源码 section 合并为一条 token 流（而非拼接字符串），每个 token 的诊断信息保留其 section 名。context 的 `Prepare → SetArg → Execute → GetReturn` 流程镜像 AngelScript；在教学 API 中用 RAII 替代公开的引用计数调用。测试覆盖完整公开管线、跨 section 编译、module 创建策略、类型化参数校验与诊断转发。

## 相关文档

- [docs/stages/09-engine-module-context.md](../stages/09-engine-module-context.md)

## 涉及文件

```
 CMakeLists.txt                          |   1 +
 docs/stages/09-engine-module-context.md |  15 +++
 include/mini_as/engine.hpp              |  73 ++++++++++++++++++
 src/engine.cpp                          | 131 +++++++++++++++++++++++++++++++-
 tests/test_engine.cpp                   |  31 ++++++++
 5 files changed, 249 insertions(+), 2 deletions(-)
```
