## 总体结论

提交 `2f9b7dd2b6c0c82883dc89cd8cf1213397e0c20f` 是一个完整的“脚本函数调用”里程碑：

- 编译器从单遍函数生成改为“预分配函数槽位 + 编译函数体”。
- `CALL` 从占位指令变成可执行指令。
- VM 引入显式调用帧，支持递归、前向调用和调用者局部变量恢复。
- 主流程设计自洽，现有 17 个测试全部通过。
- 最大的架构风险是 `callTargets` 使用裸指针，使 `BytecodeModule` 实际上不再能被安全复制。

提交信息：

- 作者：`sunlaibing`
- 时间：2026-07-30 01:08:44 +0800
- 规模：6 个文件，新增 141 行、删除 20 行
- 提交说明：`feat: add two pass functions and explicit call frames`

## 提交前的问题

类型检查器原本已经会预声明所有函数，因此语义检查阶段能够识别后置声明和递归函数。

但后续链路没有真正支持调用：

- 字节码编译器遇到 `Call` 会直接报错。
- VM 遇到 `Call` 或 `CallHost` 会抛出“不支持”异常。
- 编译器逐个追加函数，没有预先建立稳定的函数编号。

因此此前虽然类型检查能理解 `factorial(n)`，却不能生成和执行对应字节码。

## 两遍函数编译

核心入口位于 [src/bytecode.cpp](/Users/sun/Documents/GitHub/mini-as/src/bytecode.cpp:56)。

第一遍遍历函数签名：

1. 跳过 `host` 函数。
2. 为每个脚本函数预先创建一个 `BytecodeFunction`。
3. 使用完整声明作为键，记录声明到函数索引的映射。

例如：

```text
int entry(int)     -> 0
int factorial(int) -> 1
```

这里使用完整声明而不是函数名，是为了给函数重载留出空间。

第二遍遍历 AST：

- 根据 AST 重建函数签名。
- 在 `functionIndices_` 中找到预分配槽位。
- 把函数体编译到固定位置，而不是继续向 vector 追加。

相关实现见 [src/bytecode.cpp](/Users/sun/Documents/GitHub/mini-as/src/bytecode.cpp:67)。

这保证了在编译 `entry` 时，即使 `factorial` 的函数体尚未生成，它的索引也已经存在；递归调用同理。

严格来说，后面还有一个链接阶段：[src/bytecode.cpp](/Users/sun/Documents/GitHub/mini-as/src/bytecode.cpp:75) 为每个函数建立索引到函数对象指针的 `callTargets` 表。

## 调用指令生成

新增的 `CompileCall` 位于 [src/bytecode.cpp](/Users/sun/Documents/GitHub/mini-as/src/bytecode.cpp:255)，处理过程是：

1. 当前只接受标识符形式的调用，不支持方法调用或函数值调用。
2. 收集实参 AST。
3. 在所有非 host 签名中进行重载匹配。
4. 精确匹配代价为 `0`，`int -> float` 隐式转换代价为 `1`。
5. 按源码顺序编译实参。
6. 必要时插入 `TO_FLOAT`。
7. 发出 `CALL <function-index>`。

因此：

```cpp
float f(float x);
f(1);
```

会生成大致如下的字节码：

```text
PUSH_CONST 1
TO_FLOAT
CALL <f-index>
```

重载解析逻辑与类型检查器保持一致，但目前是两份独立实现，这是后续维护风险之一。

## 显式调用帧

调用帧定义在 [include/mini_as/vm.hpp](/Users/sun/Documents/GitHub/mini-as/include/mini_as/vm.hpp:28)，保存三项状态：

```cpp
const BytecodeFunction* function;
std::size_t pc;
std::vector<Value> locals;
```

也就是调用者的：

- 当前函数
- 返回地址
- 全部局部变量

操作数栈没有放进调用帧，而是由所有调用共享。

### 执行 `CALL`

实现位于 [src/vm.cpp](/Users/sun/Documents/GitHub/mini-as/src/vm.cpp:136)。

VM 会：

1. 校验函数索引。
2. 检查调用深度是否达到 1024。
3. 逆序弹出实参，恢复其原始参数顺序。
4. 将调用者状态压入 `callStack_`。
5. 切换到被调用函数。
6. 将 PC 重置为 0。
7. 创建新的 locals，并把参数放进前几个局部槽位。

逆序弹出很重要，因为参数是按从左到右的顺序入栈的。

### 执行 `RET`

实现位于 [src/vm.cpp](/Users/sun/Documents/GitHub/mini-as/src/vm.cpp:108)。

- 如果调用栈为空，说明返回的是入口函数，VM 进入 `Finished`。
- 否则弹出调用帧，恢复调用者函数、PC 和 locals，再把返回值压回操作数栈。

因此 `twice(x) + saved` 的执行可以表示为：

```text
caller stack: [..., twice参数]
CALL
callee stack: [...]
RET 20
caller stack: [..., 20]
LOAD saved
ADD
```

调用者尚未完成的表达式操作数仍留在共享栈中，而局部变量通过调用帧完整恢复。

## 测试覆盖

提交新增两个端到端测试：

- [tests/test_engine.cpp](/Users/sun/Documents/GitHub/mini-as/tests/test_engine.cpp:32)：验证前向调用和递归，计算 `factorial(6) == 720`。
- [tests/test_engine.cpp](/Users/sun/Documents/GitHub/mini-as/tests/test_engine.cpp:46)：验证嵌套调用后 caller locals 没有丢失，结果为 `twice(10) + 11 == 31`。

实际验证结果：

```text
17 tests passed
100% tests passed
```

构建成功，提交也通过 `git diff-tree --check`。

## 风险与不足

最值得关注的是 [include/mini_as/bytecode.hpp](/Users/sun/Documents/GitHub/mini-as/include/mini_as/bytecode.hpp:34) 中的：

```cpp
std::vector<const BytecodeFunction*> callTargets;
```

它带来以下约束：

- `BytecodeModule` 默认仍然可复制，但复制后的 `callTargets` 仍指向原模块。
- 原模块销毁后，复制模块中的指针会悬空。
- 单独复制或移出一个包含调用的 `BytecodeFunction` 同样不安全。
- 模块重新构建也会让已准备 Context 持有的旧函数指针失效。

当前正常路径主要使用 move，因此测试没有暴露问题，但类型系统并没有表达这一生命周期限制。

其次，每个函数都保存完整函数指针表，空间复杂度是 `O(F²)`。实际上 `CALL` 已经保存模块级函数索引，更稳健的设计是让 VM 持有模块或共享链接表，再用索引直接查找。

其他测试缺口包括：

- 达到 1024 层时是否返回预期脚本异常。
- 暂停发生在深层调用中时能否正确恢复。
- 被调用函数中的异常位置。
- 多参数求值顺序和 `int -> float` 转换。
- 重载函数调用。
- 模块复制、移动和重新构建后的生命周期行为。

## 评价

这是一个实现路径清晰、编译器与 VM 契约匹配的提交。它真正打通了：

```text
函数预声明 -> 固定函数索引 -> CALL 生成 -> 显式帧切换 -> RET 恢复
```

功能正确性已经由端到端测试覆盖。主要问题不在调用算法，而在链接表示：裸函数指针让字节码模块具备隐含的地址稳定性和不可复制约束。作为教学阶段实现是合理的，但如果继续向可复用运行时演进，应优先收紧或重构这部分所有权模型。
