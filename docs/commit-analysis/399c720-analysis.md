# `399c720` 提交分析：Trial-Deletion Cycle GC

## 1. 提交概况

- 完整提交：`399c720c75169021951fc5702cff64dfbc3d29ac`
- 父提交：`668ce8e150bfdfadd4c5d24fb10629d28af9baca`
- 作者：`sunlaibing <sunlaibing88@gmail.com>`
- 时间：`2026-07-30 01:18:39 +08:00`
- 标题：`feat: collect script object cycles with trial deletion`
- 规模：7 个文件，新增 144 行、删除 1 行

本提交保留引用计数对普通对象图的确定性回收，同时增加一个显式触发、stop-the-world 的 trial-deletion 收集器，解决脚本对象之间形成环后引用计数永远不归零的问题。

## 2. 为什么引用计数不足

对于两个互相引用、外部已经不可达的对象：

```text
A.next -> B
B.next -> A
```

A 和 B 的真实引用计数都为 1，最后一个外部 handle 释放后仍不会降到 0。单纯 `Release` 无法判断这两个引用都来自待回收集合内部。

本提交通过比较“真实引用数”和“候选集合内部入边数”识别外部根。

## 3. 候选对象跟踪

`TypeInfo` 新增 `GarbageCollector* collector`。engine 注册具体脚本类型时，把自己的 collector 写入类型元数据；接口类型和宿主类型不关联 collector。

`ScriptObject` 构造时向 collector 注册自身，`RefObject` 析构时注销自身。collector 使用 `std::unordered_set<RefObject*>` 保存候选对象，因此：

- 普通引用计数归零销毁后，候选表不会残留悬空项。
- 存在循环的脚本对象持续留在候选表中，等待显式收集。
- 宿主对象默认不参与本收集器。

engine 新增 `CollectGarbage()` 和 `GetTrackedObjectCount()` 作为控制与观察 API。

## 4. 引用枚举

`ScriptObject::EnumerateReferences` 遍历所有字段，只对非空 `ObjectHandle` 调用 visitor。由此得到候选对象图中的有向边。

`ClearReferences` 把所有对象字段替换为 null handle。替换动作通过正常的 `Value`/`ObjectHandle` 赋值释放旧引用，因此 GC 不需要直接修改引用计数内部状态。

这是该实现的重要优点：收集器只负责判定垃圾和断环，最终销毁仍由原有引用计数协议完成。

## 5. 收集算法

`GarbageCollector::Collect` 分为五步：

1. 快照当前候选集合。
2. 遍历所有候选引用，统计每个候选对象收到的内部入边数量。
3. 若 `RefCount() > internalIncoming`，说明对象至少有一个候选集合外引用，将其标记为外部根。
4. 从所有外部根沿对象字段做可达性标记。
5. 未标记对象即垃圾；临时 AddRef 后清空其对象字段，再 Release 临时引用。

判断公式是：

```text
external references = real refcount - incoming edges from candidate set
```

只要 external references 大于 0，对象及其能到达的候选对象就必须保留。

## 6. 临时引用的作用

断环之前，collector 先对所有待回收对象 `AddRef`。这是必要的稳定措施。

若直接逐个清字段，第一个对象释放的边可能让另一个对象立即归零析构，随后 collector 仍会访问已失效的 raw pointer。临时引用确保全部垃圾对象至少活到清边阶段完成；最后逐个 Release 临时引用，正常触发析构和候选表注销。

## 7. 复杂度

令候选对象数为 V、候选对象间引用边数为 E：

- 快照与初始化：`O(V)`
- 统计内部入边：`O(V + E)`
- 可达性标记：`O(V + E)`
- 清理：`O(V + E_garbage)`
- 额外空间：`O(V)`

这适合教学和小规模显式收集，不具备增量 GC 的暂停时间控制。

## 8. 测试覆盖

新增 `tests/test_gc.cpp`，测试用例从 26 增至 29：

- 无外部根的两个对象互引用，回收数为 2。
- 自引用对象在外部 handle 释放后被回收。
- 有外部 handle 的循环图不被误收集。
- 回收后 tracked count 归零。

三个测试同时验证了“保活”和“回收”两个方向，而不只是检查对象数量下降。

## 9. 风险与限制

1. 收集器没有析构清扫。engine 销毁时若仍有不可达循环，候选 set 自身销毁但对象不会自动断环，形成泄漏。
2. `RefObject` 析构通过 `type_->collector` 注销；若外部 handle 或循环对象活得比 engine 和 `TypeInfo` 更久，会访问悬空类型/collector 指针。
3. 算法要求 stop-the-world，但 API 和实现没有并发保护。原子引用计数不能使候选集合、字段枚举或清理过程线程安全。
4. GC 正确性依赖每个参与对象准确枚举全部强引用。漏报会导致错误回收，多报会导致保守泄漏。
5. 只有 `ScriptObject` 自动注册；包含脚本对象引用的自定义宿主对象不会成为候选图节点，除非未来建立明确注册协议。
6. `Collect()` 返回判定为 garbage 的数量。当前候选均为 `ScriptObject`，所以等于实际销毁数；若候选类型扩展而不能 `ClearReferences`，两者可能不再相等。
7. 每次收集都会扫描全部候选对象，没有分代、增量、预算或自动触发策略。

## 10. 结论

算法本身结构清楚，根判断、可达性传播和临时保活都正确体现了 trial deletion 的核心。它与现有 RAII 引用计数组合得很好，没有引入绕过 `ObjectHandle` 的特殊销毁路径。当前主要缺口不是基本算法，而是 engine 销毁语义、跨线程约束和可枚举对象协议尚未被 API 明确表达。
