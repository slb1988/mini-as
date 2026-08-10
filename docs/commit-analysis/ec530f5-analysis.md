# `ec530f5` 提交分析：Generic Host Bridge

## 1. 提交概况

- 完整提交：`ec530f5bd932e9ed2d234c7fbf702645d3e0ac87`
- 父提交：`2f9b7dd2b6c0c82883dc89cd8cf1213397e0c20f`
- 作者：`sunlaibing <sunlaibing88@gmail.com>`
- 时间：`2026-07-30 01:10:45 +08:00`
- 标题：`feat: bridge registered host functions through generic calls`
- 规模：10 个文件，新增 267 行、删除 8 行

本提交把上一阶段已经保留的 `CALL_HOST` 占位指令接通，使脚本能够调用由宿主注册的 C++ 回调。实现选择可移植的类型擦除桥接，而不是依赖平台 ABI 的原生调用桩。

## 2. 解决的问题

父提交只能执行脚本函数。类型系统虽然有 `FunctionSignature::host`，字节码也已经定义 `CALL_HOST`，但缺少以下环节：

1. 对外的宿主函数注册 API。
2. 注册声明字符串到内部类型签名的解析。
3. 编译期对宿主重载的选择和参数转换。
4. VM 到 C++ 回调之间的参数、返回值和异常协议。
5. 宿主回调对象的稳定所有权。

本提交把这些环节串成一条完整链路。

## 3. 公共 API 与数据模型

新增 `include/mini_as/generic.hpp`，核心类型为：

- `GenericCall`：暴露类型化参数读取、返回值设置和脚本异常设置。
- `GenericFunction`：`std::function<void(GenericCall&)>` 类型擦除回调。
- `RegisteredHostFunction`：将 `FunctionSignature` 和回调绑定在一起。
- `ParseFunctionDeclaration`：把注册字符串解析成统一签名。

`ScriptEngine::RegisterGlobalFunction` 完成注册校验和持久化。宿主函数存放在 `std::deque<RegisteredHostFunction>` 中，这是有意的地址稳定性选择：后续注册不会像 `std::vector` 扩容那样移动已有元素，已编译字节码保存的宿主函数指针因而仍然有效。

## 4. 声明解析

`src/generic.cpp` 使用项目现有 `Tokenizer` 解析诸如：

```cpp
float Scale(float value)
void Print(string &in)
Thing@ Identity(Thing@ value)
```

解析器复用了 `DataType`，支持 `void`、`bool`、`int`、`float`、`string`、对象名和 `@` 句柄。它先把 `&` 替换为空格，再忽略参数类型后到逗号或右括号之间的文本，所以参数名和 `in` 等方向修饰词不会进入签名。

这种做法实现简单并能接受 AngelScript 风格的注册文本，但语法校验偏宽松：参数类型后的未知 token 也可能被直接跳过，而不是诊断为非法声明。

## 5. 编译链路

模块构建时首先把 engine 中的宿主签名注册到 `TypeChecker`。因此脚本调用与宿主调用共享同一套重载规则：

- 完全匹配代价为 0。
- `int -> float` 代价为 1。
- 其他转换不可行。

`BytecodeCompiler` 新增 `hostIndices_`，按注册顺序给宿主函数分配索引。`CompileCall` 不再排除 host 签名；选中目标后：

- 脚本目标生成 `CALL script-index`。
- 宿主目标生成 `CALL_HOST host-index`。

参数仍按源码顺序求值，必要时在调用前插入 `TO_FLOAT`。模块链接阶段再把 engine 中稳定的 `RegisteredHostFunction*` 填入每个函数的 `hostTargets`。

## 6. VM 执行协议

执行 `CALL_HOST` 时，VM：

1. 校验宿主目标索引。
2. 根据签名参数数量逆序弹出实参，恢复声明顺序。
3. 创建只在本次调用期间有效的 `GenericCall`。
4. 同步调用 C++ 回调。
5. 将 `std::exception` 转换成带 `host exception:` 前缀的脚本异常。
6. 优先检查回调通过 `SetException` 设置的业务异常。
7. 严格校验返回值类型是否等于声明返回类型。
8. 把返回值压回 VM 操作数栈。

这是同步、类型已检查的边界。`GenericCall` 持有参数 vector 的引用，但 vector 覆盖整个回调调用期，因此在正常回调范围内有效。

## 7. 行为示例

脚本：

```angelscript
float run(int x) { return Scale(x) + 1; }
```

宿主声明 `float Scale(float value)` 时，关键字节码语义为：

```text
LOAD_LOCAL x
TO_FLOAT
CALL_HOST Scale
PUSH_CONST 1
ADD_F
RET
```

类型转换发生在编译期选定的调用点，宿主回调拿到的已经是 `float`。

## 8. 测试覆盖

新增 `tests/test_generic.cpp`，使测试用例数从 17 增至 20，覆盖：

- `int -> float` 参数转换、类型化取参和浮点返回。
- `GenericCall::SetException` 到脚本 `ExecutionState::Exception` 的传播。
- 异常对应脚本调用点的源码 section。
- 非法注册声明。
- `string &in` 风格文本。
- 规范化签名后的重复注册拒绝。

## 9. 风险与限制

1. `hostTargets` 是指向 engine 内部对象的裸指针。模块和函数不能安全地脱离所属 engine 生命周期执行。
2. `BytecodeFunction` 和 `BytecodeModule` 仍可默认复制，复制不会重定位内部的 `hostTargets` 和 `callTargets` 指针。
3. 声明解析器会跳过参数类型后的任意文本，错误注册文本可能被过度接受。
4. VM 只捕获继承自 `std::exception` 的异常，其他 C++ 异常会越过脚本异常边界。
5. 类型检查器和字节码编译器分别实现了一次重载选择；当前输入顺序相同所以结果一致，但规则演进时容易分叉。
6. 返回值必须精确匹配声明类型，不执行返回方向的隐式转换。这是明确而严格的协议，但宿主实现必须自行遵守。
7. `git diff-tree --check` 报告 3 个新增文件存在 EOF 多余空行，属于格式问题，不影响行为。

## 10. 结论

该提交以较小的抽象面完成了完整宿主调用闭环。最合理的设计决定是复用静态类型签名，并用 `GenericCall` 隔离 VM 值与平台 ABI；最需要后续治理的是裸指针链接表、声明解析宽松度，以及重载解析逻辑重复。
