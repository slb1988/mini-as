该提交是仓库架构上的分水岭：它没有扩展脚本语言本身，而是在既有 Tokenizer、Parser、TypeChecker、BytecodeCompiler、VM 之上增加了一层面向宿主程序的 AngelScript 风格 API。

目标提交：

- Commit：`4c3a44ddde38f5edbae45d3a2b988c19f1bad36e`
- Parent：`883b1cd70c600157f67cad242382cf2857596ed8`
- 时间：2026-07-30 01:06:48 +08:00
- 规模：5 个文件，新增 249 行、删除 2 行
- 当前工作区正处于该提交的 detached HEAD

**一、提交所处上下文**

父提交已经完成了：

```text
源代码
  -> Tokenizer
  -> Parser / AST
  -> TypeChecker
  -> BytecodeCompiler
  -> VirtualMachine
```

但这些还是彼此独立的底层组件。宿主程序需要自己组织诊断、编译、保存字节码并驱动 VM。

本提交把它们包装成：

```text
ScriptEngine
  ├── 拥有命名 ScriptModule
  │     ├── 收集多个源码 section
  │     └── 保存 BytecodeModule / BytecodeFunction
  └── 创建 ScriptContext
        ├── Prepare(function)
        ├── SetArg*
        ├── Execute()
        └── GetReturn*
```

这与官方 AngelScript 的 `asIScriptEngine -> asIScriptModule -> asIScriptContext` 使用流程高度对应，但使用 `unique_ptr` 代替公开的 `AddRef/Release`。

紧随其后的 `2f9b7dd` 才增加脚本函数调用、递归和调用帧；再往后才接入宿主函数、对象、GC 和完整上下文控制。因此，本提交主要是在搭建后续能力所依赖的宿主 API 边界。

**二、文件级修改**

| 文件 | 作用 |
|---|---|
| [CMakeLists.txt](/Users/sun/Documents/GitHub/mini-as/CMakeLists.txt:27) | 将 `tests/test_engine.cpp` 加入测试程序 |
| [engine.hpp](/Users/sun/Documents/GitHub/mini-as/include/mini_as/engine.hpp:15) | 定义 Engine、Module、Context 三层公开 API |
| [engine.cpp](/Users/sun/Documents/GitHub/mini-as/src/engine.cpp:18) | 实现源码构建、模块管理、参数设置和执行流程 |
| [test_engine.cpp](/Users/sun/Documents/GitHub/mini-as/tests/test_engine.cpp:4) | 增加两个端到端测试 |
| [09-engine-module-context.md](/Users/sun/Documents/GitHub/mini-as/docs/stages/09-engine-module-context.md:1) | 说明生命周期分层设计 |

**三、ScriptEngine**

[ScriptEngine](/Users/sun/Documents/GitHub/mini-as/include/mini_as/engine.hpp:64) 负责全局配置和模块所有权：

```cpp
std::unordered_map<std::string, std::unique_ptr<ScriptModule>> modules_;
MessageCallback messageCallback_;
```

`GetModule()` 提供三种策略：

- `AlwaysCreate`：无条件创建；同名模块存在时直接销毁并替换。
- `CreateIfMissing`：存在就复用，不存在就创建，是默认策略。
- `OnlyIfExists`：只查询，不创建。

默认模块名是空字符串，因此也支持一个隐式默认模块。

`SetMessageCallback()` 保存全局诊断回调。模块构建期间，Tokenizer、Parser、TypeChecker 和 BytecodeCompiler 共享同一个 `DiagnosticSink`，所有诊断通过 `ForwardDiagnostic()` 转发到宿主。

这建立了统一的错误出口，但诊断没有长期保存在模块中；调用者如果需要留存，必须像测试一样在回调中复制。

**四、ScriptModule 构建过程**

[ScriptModule::Build()](/Users/sun/Documents/GitHub/mini-as/src/engine.cpp:18) 是本提交的核心：

1. 为本次构建创建 `DiagnosticSink`。
2. 分别 Tokenize 每个源码 section。
3. 删除每个 section 自带的 `End` token。
4. 将所有普通 token 移入统一 token 数组。
5. 在最后手工追加一个全局 `End`。
6. Parser 构造一棵统一 AST。
7. TypeChecker 预声明并检查全部函数。
8. BytecodeCompiler 生成候选 `BytecodeModule`。
9. 成功后替换模块字节码并清空源码 sections。

这里不是简单拼接源码字符串，而是拼接 token：

```text
section A -> tokens A ┐
                      ├-> combined token stream -> Parser
section B -> tokens B ┘
```

因此每个 token 保留自己的 `section/row/column`。测试故意把函数声明切在两个 section 中，证明语法可以跨 section 延续，同时错误仍能定位到原 section。

但 section 边界必须位于 token 边界。字符串、标识符或块注释不能从一个 section 中间延续到下一个 section；两个 section 也不会像普通字符串连接那样把相邻字符融合成一个 token。

函数查询提供两种形式：

- `GetFunctionByDecl("int calc(int, int)")`：使用规范化声明精确匹配。
- `GetFunctionByName("calc")`：只按名字查找，存在重载时返回第一个。

返回值都是指向模块内部 `vector<BytecodeFunction>` 元素的借用指针。

**五、ScriptContext**

[ScriptContext](/Users/sun/Documents/GitHub/mini-as/include/mini_as/engine.hpp:37) 将可变执行状态从模块中分离：

- 模块保存相对静态的函数和字节码。
- Context 保存参数、VM 栈、局部变量、程序计数器、返回值和异常。
- 同一函数可以为多次执行创建多个 Context。

`Prepare()` 保存函数指针、按参数数量创建参数槽，并进入 `Prepared` 状态。

`SetArgument()` 进行了三项检查：

- 已经 Prepare；
- 参数索引合法；
- Context 仍处于 `Prepared`。

类型必须完全相同，唯一允许的隐式转换是 `int -> float`，与静态类型检查器的数值提升规则一致。缺少参数赋值不会立刻失败，因为参数槽默认是空 `Value`；通常会在执行具体类型指令时形成运行时异常。

`Execute()` 首次调用时把函数和参数交给 VM；从 `Suspended` 状态再次调用时，直接执行 `Continue()`。完成后可以通过返回值或异常 getter 读取结果。

**六、测试覆盖**

新增测试覆盖了：

- Engine → Module → Build → Function → Context → Execute 的完整路径；
- 两个源码 section 跨边界构成一个函数；
- 根据规范化声明查找函数；
- 两个整数参数和整数返回值；
- `OnlyIfExists` 不创建模块；
- 类型检查错误经 Engine 回调转发；
- 错误位置保留 `"broken"` section 名。

目标快照独立构建后共有 15 个测试用例，全部通过。

文档声称测试覆盖了“typed argument validation”，但新增测试只验证了正确的 `SetArgInt`；没有覆盖错误类型、越界索引、执行后再次设置参数或 `int -> float` 转换。

**七、重要问题与限制**

最明确的问题是文档与实现不一致。

文档称“只有成功构建后才原子替换字节码”，但失败路径实际执行：

```cpp
if (!typed) {
    bytecode_ = {};
    return false;
}
```

因此一次失败重建会清空上一次成功结果。临时探针验证结果为：

```text
first_build=true
function_before_failure=true
second_build=false
function_after_failure=false
diagnostic_section=broken
```

该问题后来由提交 `3d986f4 fix(module): preserve last successful image on failed rebuild` 修复。

生命周期分离在此提交中也只是概念分层，尚未形成安全的所有权保证：

- Context 持有指向模块内部函数的裸指针。
- 模块成功重建会替换整个 `BytecodeModule`，旧函数指针失效。
- `AlwaysCreate` 替换同名模块时，旧模块指针及其函数指针立即失效。
- Engine 被销毁后，Module 和 Context 中保存的 Engine 引用也失效。
- 本提交中的 `ScriptContext::engine_` 实际未被使用。

后来的 `3d986f4` 引入共享的不可变 `ModuleImage`，让已 Prepare 的 Context 可以持有旧模块镜像，才真正解决“重建期间继续执行旧函数”的问题。

此外，`Suspend()` 在本提交里还缺少实用触发入口：首次 `Execute()` 内部的 `VM::Prepare()` 会清除预先请求的 suspend 标记，而同步执行期间又没有 line callback。直到 `9e000cc` 增加行回调后，宿主才能在执行过程中可靠地暂停或中止。

**八、总体评价**

这是一个方向正确、接口形态清晰的架构提交：它把底层编译和 VM 组件提升为可嵌入式脚本引擎工作流，并为后续函数调用、宿主绑定、对象系统和上下文控制提供了稳定入口。

但提交标题中的“separate lifecycles”更多是 API 职责分离，而不是完整的生命周期安全。失败重建、函数裸指针和模块替换问题说明当时的所有权模型还没有闭环；后续引入不可变模块镜像正是对这一提交设计缺口的补完。