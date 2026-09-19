# docs: add repository agent guide

- Commit: 9022e083b36cabd3f742d117117f985a35fd5dcc
- Date: 2026-08-09 09:39:13 +0800
- Author: sunlaibing

## 变更内容

新增 `AGENTS.md` 仓库代理指南，沉淀项目护栏与架构约定，供 AI 协作者（及人类贡献者）遵循：

- 项目定位：AngelScript 2.38.0 是语义参照，但本仓库是教学实现而非 SDK 克隆；明确保留 typed bytecode VM、`std::variant` 版 `Value`、`GenericCall` portable 宿主桥（禁止原生 ABI/汇编桥）、`mini_as` API 兼容（官方风格 API 归属未来的 `mini_as::compat` 门面）
- 编译管线与所有权规则：`ModuleImage` 快照、重建失败保留旧镜像、context 持有镜像、稳定 ID 体系、module 全局共享语义
- 关键编译器抽象的使用约定：`LValueRef`（扩展它而不是为每个运算符单写赋值）、`CallableRef`、`FunctionSignature::Declaration()` 作为持久键、默认/命名参数在调用点物化、copy-in/copy-out、共享 `ConstantExpressionEvaluator`

## 相关文档

- [AGENTS.md](../../AGENTS.md)

## 涉及文件

```
 AGENTS.md | 171 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 1 file changed, 171 insertions(+)
```
