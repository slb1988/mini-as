# feat(gc): expose statistics and circular reference callbacks

- Commit: d577e729ed05476a536dcb6fb819fc6506784c1a
- Date: 2026-08-11 23:30:18 +0800
- Author: sunlaibing

## 变更内容

宿主可监控 GC 压力并在检测到环时检查对象：

- `GetGarbageCollectionStatistics()` 镜像 AngelScript 2.38.0 的统计类别：`currentSize`（当前被跟踪对象数）、`totalDestroyed`（引擎创建以来销毁的跟踪对象总数）、`totalDetected`（被分类为循环垃圾的对象数）、`newObjects`（尚未进入检测周期的跟踪对象）、`totalNewDestroyed`（检测前就被引用计数销毁的新对象数）；计数器对引擎生命周期单调（两个 current  gauge 除外）；增量检测在周期开始时把当前新对象集并入活动快照
- `SetCircularReferenceDetectedCallback`：对每个被分类为循环垃圾的对象在「分类后、清引用前」回调一次，提供只读的类型与对象指针供调试器检查具体字段或宿主状态；与官方一致，回调中修改或保留被报告对象是不受支持的（回收已对检测集合提交）
- compat facade 暴露官方风格 GC 标志、`GarbageCollect`、`GetGCStatistics`、`SetCircularRefDetectedCallback`，转发到 native 增量收集器；整数计数器在窄化到官方 32 位表面时饱和

## 相关文档

- [docs/stages/86-gc-statistics-and-callbacks.md](../stages/86-gc-statistics-and-callbacks.md)

## 涉及文件

```
 AGENTS.md                                     |  9 +++--
 docs/stages/86-gc-statistics-and-callbacks.md | 43 ++++++++++++++++++++++++
 include/mini_as/compat.hpp                    | 13 ++++++++
 include/mini_as/engine.hpp                    |  4 +++
 include/mini_as/object.hpp                    | 17 ++++++++++
 src/compat.cpp                                | 32 ++++++++++++++++++
 src/engine.cpp                                | 10 ++++++
 src/object.cpp                                | 29 ++++++++++++++---
 tests/test_compat.cpp                         | 34 +++++++++++++++++++
 tests/test_gc.cpp                             | 47 +++++++++++++++++++++++++++
 10 files changed, 232 insertions(+), 6 deletions(-)
```
