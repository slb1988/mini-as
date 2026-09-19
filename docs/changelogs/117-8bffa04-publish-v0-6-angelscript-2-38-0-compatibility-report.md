# docs: publish v0.6 AngelScript 2.38.0 compatibility report

- Commit: 8bffa04e3aad69f3fe122ead1da9e72533e0c33e
- Date: 2026-08-12 01:00:56 +0800
- Author: sunlaibing

## 变更内容

发布 v0.6 兼容性报告，宣告 v0.2–v0.6 对齐路线图完成：每个路线图特性都有独立实现 commit 与对应 `docs/stages/` 阶段笔记。

- v0.6 新增维度：模板与标准 add-on（注册模板类型、array、初始化列表、索引、foreach、dictionary、any/ref、注册模板函数）、高级 module 组合（import、shared、external shared、mixin）、增量 GC 与统计回调、context pooling 与协作协程、活状态序列化（module 全局/对象图、挂起 context）、变参宿主调用
- 重申边界：兼容指「文档化子集的可观察源码行为一致」，不含 SDK 头文件/ABI/原生调用约定/bytecode 兼容；当前私有 bytecode 格式为 `MASB` v8
- 验收数据：最终套件 69 个 CTest 条目（2 个 native 测试程序 + 67 个差分用例）；MSVC 2022 `/W4` 构建 + 381 个单元测试通过；MinGW GCC 13 `-Wall -Wextra -Wpedantic` 全部通过；Clang 与 ASan/UBSan 由 CI 负责
- 明确剩余缺口：完整模板语义（推断、嵌套替换、特化、脚本定义模板）、add-on 全表面与边角、完整官方反射接口与行为注册、无锁多线程执行、生产级 GC、任意宿主资源的可移植序列化、外围文件/网络/日期时间库
- 同时更新 README 与 `00-roadmap.md` 指向该报告

## 相关文档

- [docs/compatibility-v0.6.md](../compatibility-v0.6.md)
- [docs/stages/00-roadmap.md](../stages/00-roadmap.md)

## 涉及文件

```
 AGENTS.md                  |  19 ++++----
 README.md                  |  38 ++++++++-------
 docs/compatibility-v0.6.md | 115 +++++++++++++++++++++++++++++++++++++++++++++
 docs/stages/00-roadmap.md  |   4 ++
 4 files changed, 150 insertions(+), 26 deletions(-)
```
