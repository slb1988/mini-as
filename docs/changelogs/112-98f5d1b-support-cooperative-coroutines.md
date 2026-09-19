# feat(runtime): support cooperative coroutines

- Commit: 98f5d1b049499af465ade3a2711f1ead1ed7a128
- Date: 2026-08-11 23:44:07 +0800
- Author: sunlaibing

## 变更内容

在既有 VM 暂停/恢复机制与 stage 87 context pool 之上提供小型宿主驱动协程调度器，模型跟随 AngelScript 2.38.0 的 `contextmgr` add-on：脚本自愿 yield，宿主每个应用 tick 推进所有就绪 context。

- `CoroutineScheduler` 持有从同一 `ScriptEngine` 请求的 context，提供：`RegisterYieldFunction()`（脚本可见的 `void yield()`）、`Start()`（带类型初参启动全局脚本函数）、`ExecuteRound()`（每个就绪协程一片的公平调度）、`Yield()`/`Abort()`/`AbortAll()`、稳定 `CoroutineId` 与保留的 `CoroutineResult`（在 context 归还引擎前拷贝返回值或异常文本、位置与栈帧）；`TakeCompleted()` 把结果移交给宿主
- **公平性**：编译器本就在每条语句边界发射 `Suspend` cue，`yield()` 请求挂起使当前语句完成后在下一 cue 返回；`ExecuteRound()` 进入时快照就绪任务数，回调中新建的协程等下一轮，防止一个生产者饿死存量任务；挂起任务追加回就绪队列，结束/中止/抛异常的任务移除并经 `ReturnContext()` 归还——应用 context 池对调度器可用且无重复所有权规则
- **安全取消**：不能在宿主回调内直接清 VM 栈——调度器记录请求、让 VM 挂起，`Execute()` 返回后才 `Abort()`；队列中的 context 可立即中止；在普通 context 或调度器已销毁后调用 `yield()` 是带位置运行时异常而非解引用失效状态
- 调度器刻意单线程（同官方 add-on），必须先于引擎销毁（析构时归还所有未归还 context）

## 相关文档

- [docs/stages/88-cooperative-coroutines.md](../stages/88-cooperative-coroutines.md)

## 涉及文件

```
 AGENTS.md                                |  11 +++---
 CMakeLists.txt                           |   2 +
 docs/stages/88-cooperative-coroutines.md |  57 ++++++++++++++
 include/mini_as/coroutine.hpp            |  54 ++++++++++++++
 src/coroutine.cpp                        | 169 ++++++++++++++++++++++++++++++++
 tests/test_coroutine.cpp                 | 186 +++++++++++++++++++++++++++++++++++++++
 6 files changed, 477 insertions(+), 2 deletions(-)
```
