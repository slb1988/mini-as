# fix(module): preserve incremental image state

- Commit: f9779975439751cbf85447f7cd8fc273e19d4b50
- Date: 2026-08-11 12:59:43 +0800
- Author: sunlaibing

## 变更内容

修复增量编译时镜像状态丢失的两处问题：

- `CompileFunction` 的候选 `BytecodeModule` 现在继承当前镜像的 `globalInitializer`——否则动态编译会丢掉全局初始化 bytecode
- 下一个镜像继承 `removedFunctions` 集合——否则移除操作会被后续增量编译「复活」，被移除函数重新可见

回归测试验证动态编译后全局初始化指令数不变，以及无关增量编译后被移除函数仍不可见。

## 涉及文件

```
 src/engine.cpp        | 2 ++
 tests/test_engine.cpp | 6 ++++++
 2 files changed, 8 insertions(+)
```
