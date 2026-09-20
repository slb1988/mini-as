---
name: as-compare
title: AngelScript 差分对比测试
description: 编译并运行 mini-as 与官方 AngelScript 2.38.0 的差分对比测试（tests/compat/compare.cmake）。当用户要求跑 compat 对比测试、验证 mini_as 与官方 AngelScript 输出一致性时使用。
tags: [AngelScript, Testing, CMake, CTest, Differential-Testing]
---

# as-compare：AngelScript 差分对比测试

`tests/compat/compare.cmake` 是 CMake 脚本模式文件（`cmake -P` 执行），分别用 mini_as 和官方 AngelScript 两个 runner 跑同一个 `.as` 脚本并比较 stdout（CRLF 已归一化）。任一 runner 非零退出或输出不一致即失败。

## 标准流程（CTest 方式，推荐）

在仓库根目录（D:/Github/mini-as）执行：

```bash
# 1. 配置（首次或 CMakeLists 变更后；会从网络下载 AngelScript 2.38.0 SDK，SHA-256 已固定）
cmake -B cmake-build-compat -DMINI_AS_BUILD_COMPAT_TESTS=ON

# 2. 编译（生成 mini_as_compat_runner 和 angelscript_compat_runner）
cmake --build cmake-build-compat

# 3. 运行差分测试
ctest --test-dir cmake-build-compat -C Debug -R compat --output-on-failure
```

## 关键坑：必须加 `-C Debug`

项目用 Visual Studio 多配置生成器，ctest 不加 `-C` 会报
`Test not available without configuration. (Missing "-C <config>"?)`。
`-C` 的值必须与构建配置一致（默认 Debug；若 `--build --config Release` 则用 `-C Release`）。

## 手动执行单个脚本（绕过 CTest）

编译出两个 runner 后，可直接对任意 `.as` 脚本做对比：

```bash
cmake \
  -DMINI_RUNNER=D:/Github/mini-as/cmake-build-compat/Debug/mini_as_compat_runner.exe \
  -DOFFICIAL_RUNNER=D:/Github/mini-as/cmake-build-compat/Debug/angelscript_compat_runner.exe \
  -DSCRIPT=D:/Github/mini-as/tests/compat/cases/arithmetic.as \
  -P D:/Github/mini-as/tests/compat/compare.cmake
```

三个 `-D` 变量（`MINI_RUNNER`、`OFFICIAL_RUNNER`、`SCRIPT`）缺一不可。输出一致则静默成功；不一致会打印两边完整输出。

## 添加新的对比用例

1. 在 `tests/compat/cases/` 下新增 `.as` 脚本。
2. 在根 `CMakeLists.txt` 的 `if(MINI_AS_BUILD_COMPAT_TESTS)` 块内仿照 `compat_arithmetic` 加一条 `add_test`。

## 注意事项

- 不开 `MINI_AS_BUILD_COMPAT_TESTS` 的普通构建完全离线、不含此测试；首次开启需联网下载 SDK。
- 对比的是 runner 打印的归一化状态/返回输出，约定见 `tests/compat/mini_runner.cpp` 和 `official_runner.cpp`。
