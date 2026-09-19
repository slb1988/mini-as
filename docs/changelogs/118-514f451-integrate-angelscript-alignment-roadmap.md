# merge: integrate AngelScript alignment roadmap

- Commit: 514f451dea05efc3e25b5f5f9969551081166021
- Date: 2026-08-12 09:09:43 +0800
- Author: sunlaibing

## 变更内容

合并提交（`Merge: 9aa4a3d 8bffa04`）：把 AngelScript 对齐路线图分支（v0.2–v0.6，stages 16–92）合入主线，与主线上既有的 commit-analysis 文档提交（`9aa4a3d`）汇合。

合入内容概览（相对合并基点，共 216 个文件、+30309/-477）：

- CI 工作流、AGENTS.md 代理指南、五份兼容性矩阵（v0.2–v0.6）与架构/循环 GC 图
- stages 16–92 全部阶段文档（本仓库重要的分阶段设计参考）
- 引擎实现大幅扩展：`bytecode.cpp` +2569、`engine.cpp` +2630、`type_checker.cpp` +2380、`parser.cpp` +1169、`vm.cpp` +1048；新增 `bytecode_io`（MASB 版本化字节码）、`state_io`（MASS/MASC 活状态序列化）、`compat`（官方风格 facade）、`coroutine`、add-on（array/dictionary/any/ref）
- 测试扩展：67 个差分用例脚本、双 runner，以及 addon/compat/coroutine/serialization 等专项测试文件

## 相关文档

- [docs/compatibility-v0.6.md](../compatibility-v0.6.md)
- [docs/stages/00-roadmap.md](../stages/00-roadmap.md)

## 涉及文件

共 216 个文件（+30309/-477）。核心部分：

```
 .github/workflows/ci.yml               |   86 +
 AGENTS.md                              |  355 +++
 CMakeLists.txt                         |  520 +++
 docs/compatibility-v0.{2..6}.md        |  502 +++
 docs/stages/{16..92}-*.md              |  新增 77 个阶段文档
 include/mini_as/*.hpp + addons/*.hpp   |  新增/扩展 20 个头文件
 src/*.cpp                              |  核心实现 +13100 余行
 tests/compat/cases/*.as                |  67 个差分用例
 tests/*.cpp                            |  测试 +7400 余行
```

完整列表见 `git show --stat 514f451`。
