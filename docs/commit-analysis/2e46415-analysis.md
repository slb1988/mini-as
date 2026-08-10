# `2e46415` 提交分析：Reference Objects

## 1. 提交概况

- 完整提交：`2e4641539279df9469e3377edd26355cbbbda07f`
- 父提交：`ec530f5bd932e9ed2d234c7fbf702645d3e0ac87`
- 作者：`sunlaibing <sunlaibing88@gmail.com>`
- 时间：`2026-07-30 01:12:54 +08:00`
- 标题：`feat: add reference object registration and deterministic lifetime`
- 规模：12 个文件，新增 222 行、删除 7 行

本提交把运行时值域从纯值类型扩展到引用对象，并用侵入式引用计数保证对象经过参数、局部变量、返回值和宿主桥接时具有确定的生命周期。

## 2. 核心设计

新增的对象模型由三个层次构成：

```text
TypeInfo       engine 拥有的稳定类型元数据
RefObject      对象基类和侵入式原子引用计数
ObjectHandle   可复制、可移动的 RAII 引用值
```

`Value::Storage` 新增 `ObjectHandle` variant 分支。这个选择很关键：VM 的 locals、操作数栈、调用参数、返回值和 `GenericCall` 原本都通过 `Value` 传递，因此它们自动获得相同的 AddRef/Release 语义，无需为对象另设清理字节码。

## 3. `ObjectHandle` 生命周期语义

`ObjectHandle` 是标准 RAII 包装：

- 从 `RefObject*` 构造时调用 `AddRef`。
- 拷贝构造增加引用计数。
- 移动构造转移指针，不增加计数。
- 拷贝赋值先增加新对象计数，再释放旧对象，能正确处理交叉引用场景。
- 移动赋值释放旧对象后接管来源指针。
- 析构调用 `Release`。

`RefObject::Release` 使用 `fetch_sub(..., memory_order_acq_rel)`，从 1 降到 0 时执行 `delete this`。计数读取和增加使用 relaxed 顺序，符合引用计数只负责生命周期、对象内容另行同步的典型假设。

## 4. 类型注册与运行时类型

`ScriptEngine::RegisterObjectType` 按名称创建 `TypeInfo`，并以 `unique_ptr` 存储在 map 中。即使 map rehash，实际 `TypeInfo` 对象地址仍保持稳定。

`Value::Type()` 对非空句柄读取对象的 `TypeInfo::name`，生成 `Object(name, true)`；空句柄生成特殊类型 `Object("<null>", true)`。`Value::ToString()` 分别输出 `<Type@>` 或 `null`。

宿主通过从 `RefObject` 派生自己的对象类型，再用注册得到的 `TypeInfo*` 构造实例。`MakeObject<T>` 提供了便捷工厂，但直接构造 `ObjectHandle(new T(...))` 同样可用。

## 5. Host Bridge 扩展

`GenericCall` 新增：

- `GetArgObject`
- `SetReturnObject`

`ScriptContext` 新增 `SetArgObject`。因此对象可以沿如下路径往返：

```text
host ObjectHandle
  -> Context argument Value
  -> script local / call argument
  -> GenericCall
  -> host callback return
  -> script return Value
  -> host ObjectHandle
```

每次跨边界都只是正常的 `Value` 拷贝或移动，对象释放由 handle 自行完成。

## 6. 类型检查边界

脚本静态类型仍使用已有的 `DataType::Object(name, isHandle)`。函数重载、参数签名和返回值校验因此无需新增独立对象类型系统。

Context 设置参数时执行精确 `DataType` 比较，所以 `Thing@` 不能接收 `Other@`。这为公共 API 提供了运行前类型保护。

值得注意的是，类型检查器已经允许 `null -> 任意句柄`，但本提交的 `ScriptContext::SetArgument` 和 host 返回值校验仍使用精确类型相等：空 `ObjectHandle` 的运行时类型是 `<null>@`，因而通过 Context 传入 `Thing@` 或从 host 返回给 `Thing@` 会被拒绝。这是静态规则与运行时边界之间尚未对齐的缺口。

## 7. 测试覆盖

新增 `tests/test_objects.cpp`，测试用例总数从 20 增至 23，覆盖：

- handle 拷贝作用域中的引用计数从 1 到 2 再回到 1。
- 最后一个 handle 析构时对象被确定销毁。
- 对象经过脚本函数和 GenericCall 后保持同一地址。
- Context 拒绝实际类型为 `Other@`、声明类型为 `Thing@` 的参数。

测试中的析构计数器同时验证了对象没有提前释放，也没有在正常非循环场景泄漏。

## 8. 所有权与生命周期约束

`RefObject` 保存裸 `const TypeInfo*`，而 `TypeInfo` 由 engine 拥有。因此实际约束是：

```text
ScriptEngine 生命周期 > TypeInfo 生命周期 > 所有对应 RefObject 生命周期
```

API 没有用类型系统强制该约束。如果外部 `ObjectHandle` 比 engine 活得更久，对象析构本身尚可运行，但之后调用 `Value::Type()`、`ToString()` 或访问 `GetTypeInfo()` 会解引用悬空指针。后续 GC 提交还会进一步依赖该元数据。

## 9. 风险与限制

1. 空句柄的运行时参数和返回值兼容性与静态 `CanConvert` 规则不一致。
2. `TypeInfo*` 与 engine 生命周期强绑定，但 `ObjectHandle` 可以自由逃逸到 engine 外部。
3. `AddRef` 和 `Release` 是公开方法；调用者不平衡地直接调用会造成计数下溢、泄漏或提前释放。
4. 原子计数只保证计数操作安全，不代表对象字段或 engine API 是线程安全的。
5. 当前仅支持精确对象名，没有继承、接口转换或运行时 cast。
6. `Value::Type()` 假定非空对象一定持有有效的非空 `TypeInfo*`；自定义宿主对象若违反协议会崩溃。
7. `git diff-tree --check` 报告 3 个新增文件存在 EOF 多余空行，不影响功能。

## 10. 结论

这次改动把引用对象作为普通 `Value` 引入，是一个简洁且可组合的设计。确定性释放路径覆盖全面，测试也验证了关键引用计数边界。主要技术债集中在 engine/type/object 的生命周期契约，以及空句柄转换在静态层和运行时层的不一致。
