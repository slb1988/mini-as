# `668ce8e` 提交分析：Script Classes and Interfaces

## 1. 提交概况

- 完整提交：`668ce8e150bfdfadd4c5d24fb10629d28af9baca`
- 父提交：`2e4641539279df9469e3377edd26355cbbbda07f`
- 作者：`sunlaibing <sunlaibing88@gmail.com>`
- 时间：`2026-07-30 01:16:58 +08:00`
- 标题：`feat: add script classes handles and interface dispatch`
- 规模：11 个文件，新增 348 行、删除 18 行

本提交在上一阶段的宿主引用对象之上增加脚本类实例、字段读写、默认工厂和最小接口元数据。它完成了对象存储与字段执行，但标题中的 “interface dispatch” 只实现到派发表查询，没有加入实际的方法调用字节码。

## 2. 类型元数据扩展

类型检查层新增 `ClassSignature`：

- 类名。
- 是否为接口。
- 声明实现的接口列表。
- 按声明顺序排列的字段及类型。
- 方法签名列表。

运行时 `TypeInfo` 同步扩展为：

- `script` 标志。
- 字段布局。
- 接口名称。
- `interfaceMethodTable` 字符串映射。

类型检查器先扫描所有 class/interface 声明，再检查函数。这使字段类型、零参数类工厂和后置声明的接口都能在表达式检查前可见。

## 3. 类与接口预声明

`TypeChecker::Predeclare` 增加两阶段处理：

1. 收集全部 class/interface 的字段和方法签名。
2. 对每个具体类，验证它是否包含接口要求的同名、同返回类型、同参数列表方法。

实现只支持 AST 中的单个接口标识；没有继承层次和多接口语法。若接口名称存在且确实是 interface，缺少方法会产生构建错误。

但未知接口或指向普通 class 的“接口”会被静默跳过，而不是报错。这意味着 `class Box : Missing` 在此阶段可能没有得到预期诊断。

## 4. 编译器与新增字节码

字节码新增三个操作：

```text
NEW_OBJECT type-index
LOAD_FIELD field-index
STORE_FIELD field-index
```

编译器接收 `checker.Classes()`，为具体类建立 `classIndices_`。类名形式的零参数调用被识别为默认工厂，例如 `Box()` 生成 `NEW_OBJECT`。

字段索引来自 `ClassSignature::fields` 的声明顺序。读取 `box.value` 时先编译对象表达式，再生成 `LOAD_FIELD`；赋值 `box.value = value` 时依次压入对象和值，再生成 `STORE_FIELD`。`STORE_FIELD` 会把已写入的值重新压栈，从而保持赋值表达式有结果这一语义。

每个 `BytecodeFunction` 新增 `objectTypes` 指针表，连接 `NEW_OBJECT` 的模块内索引与 engine 拥有的 `TypeInfo`。

## 5. `ScriptObject` 运行时布局

`ScriptObject` 继承 `RefObject`，内部用连续 `std::vector<Value>` 保存字段。构造时根据字段类型填充默认值：

- `bool` 为 `false`
- `int` 为 `0`
- `float` 为 `0.0f`
- `string` 为空串
- object 为 null handle

VM 执行字段指令时先从 `Value` 提取 `ObjectHandle`，再通过 `dynamic_cast<ScriptObject*>` 阻止宿主对象被当作脚本字段容器。null、宿主对象和越界字段索引都转换成脚本运行时异常。

对象字段本身仍然是 `Value`，所以句柄字段自然参与引用计数。

## 6. 跨 Context 对象传递

`ScriptContext::SetArgument` 扩展了对象兼容规则：

- null handle 可以传给任意 handle 参数。
- 完全相同的对象类型直接接受。
- 具体脚本类可以传给其声明实现的接口。
- 不匹配的宿主对象或脚本对象被拒绝。

这同时修补了父提交中 Context 不能接收 null handle 的问题。不过 host 函数返回 null 给具体对象句柄时，VM 的精确返回类型校验仍然值得单独验证。

## 7. 接口“派发”的实际范围

`ScriptObject::Implements` 查询类型的接口列表；`ResolveInterfaceMethod` 使用：

```text
InterfaceName::method declaration
```

作为键，返回：

```text
ConcreteClass::method declaration
```

这只是可查询的派发元数据。本提交没有：

- 编译类方法函数体。
- 为方法分配可执行函数索引。
- 引入 `this`。
- 生成 virtual/interface call 指令。
- 在 VM 中调用解析后的方法。

因此更准确的能力描述是“接口实现校验和派发表构建”，而不是完整动态接口调用。

## 8. 测试覆盖

在 `tests/test_objects.cpp` 新增 3 个测试，测试总数从 23 增至 26：

- 分配 `Box`，写入 `int` 和 `string` 字段，并把对象传到另一个 Context 读取。
- 检查 `IValue` 实现关系和 `ResolveInterfaceMethod` 返回值。
- 缺少接口所需方法时构建失败并产生诊断。

这些测试覆盖了对象工厂、字段布局、字段读写、跨 Context 引用和接口元数据，但没有实际执行类方法或接口方法。

## 9. 风险与限制

1. `ScriptEngine::RegisterScriptType` 按全 engine 的类型名称复用并覆盖 `TypeInfo`。两个模块定义同名类时，后构建模块会修改前一模块及其存量对象看到的元数据。
2. 同名宿主类型与脚本类型也会复用同一个 `TypeInfo`，缺少冲突诊断。
3. 未知接口被静默忽略，接口声明有效性检查不完整。
4. 派发表为类声明的每个方法、每个接口都创建条目，不只包含该接口实际要求的方法，元数据范围偏宽。
5. 类方法只有签名元数据，不能通过脚本执行；提交标题容易让使用者高估能力边界。
6. `objectTypes` 继续使用指向 engine 元数据的裸指针，模块不能脱离 engine 生命周期。
7. 字段运行时写入不再次验证 `Value` 类型，依赖编译器生成可信字节码。
8. 类型系统只处理直接接口关系，没有接口继承、传递转换或运行时 cast。

## 10. 结论

该提交成功建立了脚本对象的最小可执行模型：稳定字段索引、连续 Value 布局、默认工厂、字段访问和句柄传递形成完整闭环。接口部分主要是静态校验与未来派发所需的元数据准备，尚不是实际方法派发。最重要的后续架构问题是把类型身份从 engine 全局名称提升为稳定、模块安全的类型标识。
