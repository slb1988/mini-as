# ci: verify gcc clang and msvc builds

- Commit: 88981d09087e76841a5c31b91e4c3b0b8ddc4336
- Date: 2026-08-08 11:57:16 +0800
- Author: sunlaibing

## 变更内容

新增 GitHub Actions CI 工作流，三条流水线：

- **build 矩阵**：Ubuntu 上 GCC / Clang，Windows 上 MSVC（Ninja + Debug 构建并跑 CTest）
- **compatibility**：开启 `MINI_AS_BUILD_COMPAT_TESTS=ON`，在 Ubuntu 上与官方 AngelScript 2.38.0 跑差分用例（`ctest -R '^compat_'`）
- **sanitizers**：Clang + ASan/UBSan 构建并运行全部测试

## 涉及文件

```
 .github/workflows/ci.yml | 86 ++++++++++++++++++++++++++++++++++++++++++++++++
 1 file changed, 86 insertions(+)
```
