# fix(bytecode): accept indexed syntax tree archives

- Commit: bd40884c642a11eb68b1801729ed67a1615aae1a
- Date: 2026-08-11 21:46:01 +0800
- Author: sunlaibing

## 变更内容

修复 bytecode 归档加载对含方括号 token 的保留语法树的接受问题（索引表达式引入的 `[`/`]` token 在反序列化校验中未被识别）。补充含括号 token 的语法树 round-trip 测试。

## 相关文档

- [docs/stages/77-indexing-expressions.md](../stages/77-indexing-expressions.md)

## 涉及文件

```
 AGENTS.md                              |  3 ++-
 docs/stages/77-indexing-expressions.md |  3 ++-
 src/bytecode_io.cpp                    |  2 +-
 tests/test_addons.cpp                  | 12 ++++++++++++
 4 files changed, 17 insertions(+), 3 deletions(-)
```
