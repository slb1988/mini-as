# feat(gc): make cycle detection incremental

- Commit: 69fe191bc6cf9a4ed98fa66f85315913b62f2500
- Date: 2026-08-11 23:25:34 +0800
- Author: sunlaibing

## 变更内容

循环检测可分摊到普通应用 tick 中：`engine->CollectGarbageStep(8)` 每次至多访问 8 个图对象；既有 `CollectGarbage()` 保持兼容（驱动同一状态机跑完整个周期）。

- **状态机**：一次收集周期分四个有界阶段——统计被跟踪对象间的内部入边 → 把有外部引用的对象识别为 root → 从 root 标记可达 → 选出未标记对象为循环垃圾；预算是「访问的图对象数」；分类后的销毁仍是原子的：所有垃圾对象先获得临时持有再清边，最后统一释放，保持引用计数与 finalizer 不变量
- **变更安全**：跟踪对象注册、析构、`AddRef`/`Release` 推进收集器代际（generation）；两步之间图或外部根集合变化时丢弃部分分类、从新快照重启——增量周期中经弱引用获得 handle 会「救回」图而不是暴露给过期决定
- `CollectGarbageStep(0)` 是 no-op；`IsGarbageCollectionInProgress()` 报告是否存在部分周期
- 测试覆盖有界推进、零预算、变更触发重启、弱引用救回、完整收集、finalizer、delegate、闭包、managed host value 与全周期 API 兼容性；该调度细节无脚本可见的差分用例

## 相关文档

- [docs/stages/85-incremental-cycle-gc.md](../stages/85-incremental-cycle-gc.md)

## 涉及文件

```
 AGENTS.md                              |   9 ++-
 docs/stages/85-incremental-cycle-gc.md |  45 ++++++++++++
 include/mini_as/engine.hpp             |   2 +
 include/mini_as/object.hpp             |  17 +++++
 src/engine.cpp                         |  10 +++
 src/object.cpp                         | 130 ++++++++++++++++++++++++---------
 tests/test_gc.cpp                      |  58 +++++++++++++++
 7 files changed, 236 insertions(+), 35 deletions(-)
```
