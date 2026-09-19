# chore: bootstrap the educational engine project

- Commit: 753101791fc03dbaeda8fb73d989dd829044a35a
- Date: 2026-07-30 00:52:42 +0800
- Author: sunlaibing

## 变更内容

项目初始化，搭建教学用引擎骨架。确立目标：以 C++17 + CMake 自底向上复刻 AngelScript 的核心架构，整个仓库按「源码文本 → token → AST → 类型检查 → bytecode → module → 显式管理的 VM context」的路线逐阶段演进，每个阶段都是一个可构建、可测试的 commit。

具体包括：

- CMake 构建骨架与 `mini_as` 库目标，接入 CTest
- 最小的 `Engine` 类占位（`include/mini_as/engine.hpp` / `src/engine.cpp`），作为后续阶段的挂载点
- `tests/test_main.cpp` 测试入口与冒烟测试
- README 构建说明与 MIT LICENSE
- 首个阶段文档 `docs/stages/00-roadmap.md`，说明整体路线：后续阶段再引入 generic host call、reference object、script class 与 cycle collection；教学实现刻意使用标准库容器替代 AngelScript 的自定义容器，让引擎算法本身保持可见

## 相关文档

- [docs/stages/00-roadmap.md](../stages/00-roadmap.md)

## 涉及文件

```
 .gitignore                 |  5 +++++
 CMakeLists.txt             | 21 +++++++++++++++++++++
 LICENSE.md                 | 12 ++++++++++++
 README.md                  | 15 +++++++++++++++
 docs/stages/00-roadmap.md  | 10 ++++++++++
 include/mini_as/engine.hpp | 10 ++++++++++
 src/engine.cpp             | 10 ++++++++++
 tests/test_main.cpp        | 13 +++++++++++++
 8 files changed, 96 insertions(+)
```
