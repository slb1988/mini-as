# feat(runtime): support context pooling

- Commit: cf4c52fb4b9cc9318f3ad84713cda3ad6d525e7e
- Date: 2026-08-11 23:37:28 +0800
- Author: sunlaibing

## 变更内容

宿主可池化复用 script context，不改变既有 `CreateContext()` 的所有权与行为；设计跟随 AngelScript 2.38.0 的 `RequestContext`/`ReturnContext` 成对回调模型：

- `RequestContext()` 从配置的 request 回调获取裸 context，未配置时默认经 `CreateContext()` 分配；`ReturnContext()` 送交配对的 return 回调，未配置池时删除；`SetContextCallbacks()` 只接受成对配置，半配置被拒绝
- `ScriptContext::Unprepare()` 释放执行栈、参数、返回值与保留的不可变 module 镜像，恢复到 `Uninitialized` 状态
- compat facade 以官方风格结果码暴露同一流程
- 请求/归还使用裸指针是因为所有权处于 transit 中（与官方 API 一致）；池实现应立即把归还指针收回 `unique_ptr`
- 生命周期规则：context 活跃或挂起时拒绝 `Unprepare()`——调度器须先完成或中止挂起的 context 再归还通用池；line callback 属于 context 配置在 unprepare 后保留，执行状态与 module 所有权不保留；重配/清空回调只影响未来的请求与归还，所有未归还 context 仍须还回发出它们的来源

## 相关文档

- [docs/stages/87-context-pooling.md](../stages/87-context-pooling.md)

## 涉及文件

```
 AGENTS.md                         |  9 +++--
 docs/stages/87-context-pooling.md | 41 +++++++++++++++++++++
 include/mini_as/compat.hpp        |  9 +++++
 include/mini_as/engine.hpp        | 10 ++++++
 src/compat.cpp                    | 24 +++++++++++++
 src/engine.cpp                    | 35 ++++++++++++++++++
 tests/test_compat.cpp             | 36 +++++++++++++++++++
 tests/test_engine.cpp             | 76 +++++++++++++++++++++++++++++++++++++++
 8 files changed, 238 insertions(+), 2 deletions(-)
```
