---
name: commit-changelog
description: 在 mini-as 仓库中，为某个 git commit（默认 HEAD，支持传入 commit-ish）生成 docs/changelogs/<序号>-<短hash>-<slug>.md 变更说明文件。当用户要求"为这次提交生成 changelog/变更说明"、"记录本次 commit"、"补一个 changelog"时使用。若会话中用户针对本次提交有追加提问或讨论，需提炼其中有价值的信息写入该 changelog 的「补充说明」一节。
---

# commit-changelog：为提交生成 docs/changelogs 条目

本仓库约定：每次提交都在 `docs/changelogs/` 留一份说明文件，文件名 = `<序号>-<短hash>-<提交说明slug>.md`（如 `119-abc1234-support-xxx.md`），正文为中文摘要（专业术语保留英文）。

## 执行流程

### 1. 收集提交信息

在仓库根目录运行本 skill 目录下的脚本（相对路径基于本 SKILL.md 所在目录）：

```bash
bash scripts/collect.sh [commit-ish]   # 缺省为 HEAD
```

（路径相对于本 SKILL.md 所在目录；在仓库根目录执行时的完整形式是 `bash .agents/skills/commit-changelog/scripts/collect.sh`。）

输出内容：

- `FILE:` 建议的目标文件路径（序号 = 目录内最大序号 + 1，3 位补零；slug 由 commit subject 去掉 `type(scope):` 前缀后转 kebab-case，截断 60 字符）
- `META:` 完整 hash / 日期 / 作者 / 父提交
- `DOCS:` 本次提交触及的 `docs/**/*.md`（作为「相关文档」候选）
- `STAT:` `git show --stat` 结果
- 随后是完整 diff（供撰写摘要；大提交可能被截断，需要时用 `git show <path>` 补读关键文件）

若序号计算结果与预期不符（例如目录里混入非约定命名文件），先向用户确认再写。

### 2. 撰写摘要

以中文撰写「变更内容」，要求：

- **参考资料优先级**：本次提交新增/修改的 `docs/stages/*.md`、`docs/compatibility-*.md` 等文档是设计意图的第一参考；其次 commit message 与 diff。摘要要覆盖「做了什么 + 关键设计决策 + 明确排除的边界」，而不是逐文件罗列
- 专业术语、API 名、opcode、类名保持英文（如 `GenericCall`、`ModuleImage`、copy-in/copy-out）
- 无 docs 的小提交（fix/refactor/杂项）可以只写一小段
- merge 提交：说明合并双方与合入内容概览，`## 涉及文件` 只列核心部分并注明 `完整列表见 git show --stat <hash>`

### 3. 收集会话上下文中的补充信息（重要）

回顾本次会话中用户围绕这次提交的**追加提问、讨论与澄清**（例如设计取舍的口头解释、背景原因、踩坑记录、后续计划）。判断其中是否有「脱离代码无法得知、对将来的读者有价值」的信息：

- 有：在 changelog 中追加一节，用要点提炼（不要流水账式复述对话）：

  ```markdown
  ## 补充说明

  <从会话讨论中提炼的背景、决策理由、注意事项>
  ```

- 没有（纯实现性会话）：省略该节。

不确定某条信息是否值得收录时，倾向收录并向用户说明收录了什么。

### 4. 写入文件并验证

按以下模板写入 `FILE:` 指定的路径：

```markdown
# <commit subject>

- Commit: <完整 hash>
- Date: <git show 的提交日期，含时区>
- Author: <作者>

## 变更内容

<中文摘要>

## 相关文档

- [docs/stages/xx-name.md](../stages/xx-name.md)   <!-- 无相关文档则省略整节 -->

## 涉及文件

```
<git show --stat 输出>
```
```

（注意模板里代码围栏是三个反引号；「相关文档」用相对路径 `../stages/`，仓库根的文档用 `../`，如 `[AGENTS.md](../../AGENTS.md)`。）

写完后：

- `read` 复查一遍文件（笔误、术语、链接路径）
- 向用户报告：生成的文件路径、序号、摘要要点，以及「补充说明」收录了哪些会话信息
- **不要**自动 `git add`/`git commit`，由用户决定何时提交

## 注意事项

- 目标提交若不是分支 tip（如 detached HEAD 或历史提交），先生成，再在汇报中提醒用户当前 HEAD 状态
- 同一提交不要重复生成：若目录中已存在包含该短 hash 的文件，直接报告并停止，除非用户明确要求重建
- 用户也可能要求批量回填历史提交：此时按 `git log --reverse` 顺序逐提交套用本流程
