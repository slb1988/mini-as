# docs: add mini_angelscript architecture sharing report

- Commit: 3c99edd2feb1e6f518b6a2a739339002fc759636
- Date: 2026-08-08 11:48:54 +0800
- Author: sunlaibing

## 变更内容

新增面向 45–60 分钟技术分享的完整报告《从 `Print(42)` 到可嵌入虚拟机》，配套整体架构图与循环 GC 流程图（drawio 源文件 + PNG + SVG）。

报告以「编译器把隐含规则变成显式数据，虚拟机把隐含控制流变成显式状态」为主线，串起编译流水线、显式 CallFrame、generic 宿主桥接、RC + trial deletion 对象系统，并坦承教学实现的边界：模块重建与 context 生命周期无硬隔离、失败时清空旧镜像（此问题随后在 `3d986f4` 修复）、原子计数不代表线程安全、语言子集刻意缩小。

## 相关文档

- [docs/technical-sharing-report.md](../technical-sharing-report.md)
- [docs/diagrams/mini-as-architecture.svg](../diagrams/mini-as-architecture.svg)
- [docs/diagrams/mini-as-cycle-gc.svg](../diagrams/mini-as-cycle-gc.svg)

## 涉及文件

```
 docs/diagrams/mini-as-architecture.drawio | 112 ++++++++++
 docs/diagrams/mini-as-architecture.png    | Bin 0 -> 79647 bytes
 docs/diagrams/mini-as-architecture.svg    |  21 ++
 docs/diagrams/mini-as-cycle-gc.drawio     |  60 ++++++
 docs/diagrams/mini-as-cycle-gc.png        | Bin 0 -> 69198 bytes
 docs/diagrams/mini-as-cycle-gc.svg        |  16 ++
 docs/technical-sharing-report.md          | 332 ++++++++++++++++++++++++++++++
 7 files changed, 541 insertions(+)
```
