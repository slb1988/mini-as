# `9e000cc` 提交分析：Context Controls and Tutorial Clone

## 1. 提交概况

- 完整提交：`9e000ccf06f796afa45c670fc1a464b5d2b3feea`
- 父提交：`399c720c75169021951fc5702cff64dfbc3d29ac`
- 作者：`sunlaibing <sunlaibing88@gmail.com>`
- 时间：`2026-07-30 01:21:34 +08:00`
- 标题：`feat: finish context controls and tutorial clone`
- 规模：11 个文件，新增 248 行、删除 3 行

这是初始教学主线的收尾提交。代码层新增 Context 行回调和运行时调用栈快照，产品层新增可执行教程、架构说明和更完整的 README，把此前分散实现的 tokenizer、编译器、VM、host bridge 和 Context API 串成一个用户可运行的嵌入流程。

## 2. Context 行回调

`ScriptContext` 新增：

```cpp
using LineCallback = std::function<void(ScriptContext&, const SourceLocation&)>;
void SetLineCallback(LineCallback callback);
```

Context 构造时给 VM 安装一层转发 lambda。VM 只接触 `SourceLocation`，公共回调则拿到当前 `ScriptContext&`，因此宿主可以在回调中调用 `Suspend()` 或 `Abort()`，同时 VM 不需要依赖 engine 层类型。

回调触发点复用既有 `OpCode::Suspend` 行提示指令。执行顺序为：

1. 调用宿主 line callback。
2. 若回调已把 VM 标记为 `Aborted`，立即离开该指令。
3. 若收到 suspend 请求，清除一次性请求并进入 `Suspended`。
4. 否则继续执行下一条字节码。

这个顺序保证回调内的 abort 优先于 suspend。

## 3. Suspend、Resume 与 Abort 语义

暂停仍然不会展开 VM 状态。操作数栈、当前 locals、当前函数、PC 和显式调用帧全部保留在 `VirtualMachine` 成员中；再次调用 `ScriptContext::Execute()` 会进入 `vm_.Continue()`，从暂停后的 PC 继续。

行回调适合实现：

- 行数或时间预算。
- 调试器单步。
- 外部取消检查。
- 轻量 profiling。

它不是逐 opcode 回调。预算精度取决于编译器插入 `Suspend` 的语句提示位置；单个复杂表达式或宿主调用执行期间不能被抢占。

## 4. 运行时调用栈快照

`ExecutionResult` 新增 `std::vector<StackFrameInfo> callStack`，其中每帧包含：

- `functionDeclaration`
- `SourceLocation`

`VirtualMachine::Fail` 在运行时异常发生时：

1. 记录当前函数和出错指令位置。
2. 从 `callStack_` 顶部向入口逆序遍历。
3. 使用每个保存帧的 `pc - 1` 定位调用者的 CALL 指令。

最终顺序是“最内层出错函数在前，入口函数在后”。例如：

```text
inner()  division by zero
middle() call inner
outer()  call middle
```

Context 通过 `GetCallStack()` 只读暴露这份快照。

## 5. 教程闭环

新增 `examples/tutorial/main.cpp` 和 `script.as`，流程与典型 AngelScript 嵌入方式保持概念一致：

```text
CreateScriptEngine
  -> SetMessageCallback
  -> RegisterGlobalFunction(Print)
  -> RegisterGlobalFunction(GetSystemTime)
  -> GetModule / AddScriptSection / Build
  -> GetFunctionByDecl
  -> CreateContext / Prepare / SetArg
  -> Execute / GetReturnFloat
```

教程还安装 10000 行预算，预算耗尽时从回调调用 `Abort()`。脚本验证了字符串拼接、数值格式化、宿主函数、浮点运算和返回值。

CMake 新增 `tutorial_clone` 可执行文件，并把它作为第二个 CTest 测试运行，脚本路径使用源码目录绝对路径，避免测试工作目录差异。

## 6. 文档变化

README 从简单构建说明扩展为：

- 已实现能力清单。
- 教程编译与调用示例。
- 当前语言子集。
- 与完整 AngelScript SDK 的兼容边界。

新增 `docs/architecture.md`，用一条管线描述 source section 到 VM、host call、script object 和 GC 的关系；新增 stage 15 文档解释 Context 控制与堆栈快照。

这部分不是纯宣传文本，它明确列出 native ABI bridge、继承、模板、脚本异常、delegate、JIT、序列化、增量 GC 和生产级优化器不在当前范围内。

## 7. 测试覆盖

新增 `tests/test_tutorial.cpp` 中的 3 个测试，单元测试用例总数从 29 增至 32：

- 注入确定性时钟并捕获 `Print` 输出，验证完整教程结果和浮点返回。
- line callback 在第二次提示时暂停，随后恢复并完成；重新 Prepare 后再验证 callback abort。
- 三层脚本调用中除零，验证调用栈帧数以及 inner-to-outer 顺序。

此外 `tutorial_clone` 自身成为 CTest 用例，验证真实文件读取和示例程序的完整执行路径。

## 8. 行为边界与风险

1. line callback 只在语句提示点运行，不是硬实时或逐指令预算。长时间宿主调用无法由它中断。
2. Context、VM、回调和 abort 状态都不是线程同步 API；不能把 `Abort()` 当作可从任意线程调用的安全取消原语。
3. line callback 抛出的 `std::exception` 会被 VM 当作当前脚本指令异常处理，但没有像 host function 那样添加专门前缀；API 文档未定义回调是否允许抛异常。
4. 调用栈只在进入 `VirtualMachine::Fail` 的异常路径生成。诸如 PC 越界这种在 `Continue` 中直接设置状态的内部错误不会获得同样的堆栈快照。
5. 调用点位置使用保存的 `pc - 1`。这对当前 CALL 指令布局正确，但未来若引入尾调用、异步调用或多指令调用序列，需要重新定义定位规则。
6. callback 捕获 `this`；Context 不可在自身回调中被销毁，否则返回 VM 后会形成悬空执行。
7. 教程中的系统时间压缩为有符号 31 位毫秒值，适合演示，但不是稳定的长期计时接口。
8. `git diff-tree --check` 报告 4 个新增文件存在 EOF 多余空行，属于格式问题。

## 9. 与前序提交的整合意义

本提交本身新增的运行时逻辑不多，但它验证了此前四个阶段确实可以组合：

```text
显式脚本调用帧
  + GenericCall 宿主桥
  + Value/ObjectHandle 生命周期
  + ScriptObject 类型系统
  + 手动循环 GC
  + Context 回调和异常栈
  = 可嵌入、可观察、可运行的教学引擎闭环
```

它也是从“内部功能逐步实现”转向“公共使用流程可演示”的边界提交。

## 10. 结论

该提交合理地把行提示复用为 Context 控制点，并利用上一阶段的显式调用帧生成异常堆栈，新增逻辑与现有架构契合。教程和端到端测试显著提高了系统可验证性。主要限制是控制点粒度、线程安全和 stack trace 仅覆盖标准运行时异常路径，这些都应在公共 API 文档中明确。
