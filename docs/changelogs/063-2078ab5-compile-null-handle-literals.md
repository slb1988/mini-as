# fix(expressions): compile null handle literals

- Commit: 2078ab58cb7b0a7ba60f22259dbd78480f886a5e
- Date: 2026-08-09 11:34:11 +0800
- Author: sunlaibing

## 变更内容

修复常量表达式求值器不识别 `null` 字面量的问题：`KwNull` 现在求值为空 `ObjectHandle` 值（类型为 `<null>` handle），使 `null` 可用于 switch case 等编译期常量场景。附回归测试。

## 涉及文件

```
 src/constant_evaluator.cpp        |  1 +
 tests/test_constant_evaluator.cpp | 10 ++++++++++
 2 files changed, 11 insertions(+)
```
