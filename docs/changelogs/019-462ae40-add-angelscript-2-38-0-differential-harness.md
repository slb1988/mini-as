# test(compat): add AngelScript 2.38.0 differential harness

- Commit: 462ae400fe7a0b3f239433bdfe7e4d5e481fc8d9
- Date: 2026-08-08 11:54:47 +0800
- Author: sunlaibing

## 变更内容

引入与官方 AngelScript 2.38.0 的差分测试（differential testing）框架：

- CMake 选项 `MINI_AS_BUILD_COMPAT_TESTS`（默认 OFF，正常构建不访问网络）；开启后通过 FetchContent 下载官方 SDK，并用 SHA-256 锁定归档完整性
- 双 runner 设计：`mini_runner`（教学引擎）与 `official_runner`（官方引擎）分别执行同一 `.as` 用例，`compare.cmake` 要求归一化后的状态与返回输出一致
- 首个用例 `tests/compat/cases/arithmetic.as` 覆盖算术与条件分支

这为后续每个语言特性提供了「与官方行为对齐」的客观验证手段。

## 涉及文件

```
 CMakeLists.txt                   | 27 +++++++++++++++++++++++
 tests/compat/README.md           |  9 ++++++++
 tests/compat/cases/arithmetic.as |  5 +++++
 tests/compat/compare.cmake       | 28 ++++++++++++++++++++++++
 tests/compat/mini_runner.cpp     | 34 +++++++++++++++++++++++++++++
 tests/compat/official_runner.cpp | 46 ++++++++++++++++++++++++++++++++++++++++
 6 files changed, 149 insertions(+)
```
