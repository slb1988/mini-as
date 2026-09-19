# test(compat): add v0.2 acceptance corpus

- Commit: 8439f76ab1e1b8862c1bf1aaeb4b56d22583c44d
- Date: 2026-08-08 13:18:35 +0800
- Author: sunlaibing

## 变更内容

新增 v0.2 综合验收差分用例 `v02_acceptance.as`，在单个脚本中组合验证当前已实现的特性集：module 全局状态、三种循环形式、switch fall-through、复合赋值、构造器参数、实例方法与 interface 虚调用（`IScore@ score = accumulator; score.score()`）。

该用例同时跑在教学引擎与官方 AngelScript 2.38.0 上并要求输出一致，作为 v0.2 里程碑的验收门槛。

## 涉及文件

```
 CMakeLists.txt                       |  7 +++++
 tests/compat/cases/v02_acceptance.as | 58 ++++++++++++++++++++++++++++++++++++
 2 files changed, 65 insertions(+)
```
