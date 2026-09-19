# feat: collect script object cycles with trial deletion

- Commit: 399c720c75169021951fc5702cff64dfbc3d29ac
- Date: 2026-07-30 01:18:39 +0800
- Author: sunlaibing

## 变更内容

实现基于 trial deletion 的循环引用回收：

- 引用计数可以立即销毁普通对象图，但环中的成员永远到不了零；script object 因此注册为 GC candidate，并枚举其字段中持有的 handle
- 一次回收：快照真实引用计数 → 统计来自候选集内部的入边 → `real > internal` 视为外部 root → 从 root 标记可达性；未被标记的对象由临时引用托住，清空其对象字段以打断循环，再释放临时引用，让普通引用计数完成最终清理
- 这是 AngelScript incremental trial deletion 的 stop-the-world 教学版本

测试覆盖自环、两对象环、带 root 的环、回收计数，以及对象销毁后从候选集中移除。

## 相关文档

- [docs/stages/14-cycle-gc.md](../stages/14-cycle-gc.md)

## 涉及文件

```
 CMakeLists.txt             |   1 +
 docs/stages/14-cycle-gc.md |  15 +++++++++++++
 include/mini_as/engine.hpp |  3 +++
 include/mini_as/object.hpp | 18 +++++++++++++++-
 src/engine.cpp             |  4 ++++
 src/object.cpp             |  50 +++++++++++++++++++++++++++++++++++++++++++
 tests/test_gc.cpp          |  54 ++++++++++++++++++++++++++++++++++++++++++++++
 7 files changed, 144 insertions(+), 1 deletion(-)
```
