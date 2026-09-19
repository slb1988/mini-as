# feat: add script classes handles and interface dispatch

- Commit: 668ce8e150bfdfadd4c5d24fb10629d28af9baca
- Date: 2026-07-30 01:16:58 +0800
- Author: sunlaibing

## 变更内容

实现 script class、字段、handle 与 interface 分派：

- script class 元数据先于函数检查构建；字段获得稳定索引，对象布局为侵入式对象头 + 连续 `Value` 数组
- `NEW_OBJECT` 调用合成的默认 factory；`LOAD_FIELD` / `STORE_FIELD` 使用编译期索引，并在 null handle 上陷入（trap）
- 对象通过与宿主对象相同的 `ObjectHandle` 跨 context 传递，脚本实例没有特殊生命周期路径——local 赋值、返回、宿主持有都走普通 AddRef/Release 语义
- interface 刻意保持最小：构建时验证具体类具备每个要求的签名，并建立 interface → 具体方法的映射表；继承与动态方法调用 bytecode 仍在此教学子集之外

测试覆盖字段布局与执行、跨 context handle、interface 表查找与缺失方法的诊断。

## 相关文档

- [docs/stages/13-script-classes.md](../stages/13-script-classes.md)

## 涉及文件

```
 docs/stages/13-script-classes.md | 16 +++++++++
 include/mini_as/bytecode.hpp     | 10 ++++--
 include/mini_as/engine.hpp       |  1 +
 include/mini_as/object.hpp       | 25 +++++++++++++-
 include/mini_as/type_checker.hpp | 12 ++++++-
 src/bytecode.cpp                 | 61 ++++++++++++++++++++++++++++------
 src/engine.cpp                   | 46 +++++++++++++++++++++++--
 src/object.cpp                   | 42 ++++++++++++++++++++++-
 src/type_checker.cpp             | 72 +++++++++++++++++++++++++++++++++++++++-
 src/vm.cpp                       | 27 +++++++++++++++
 tests/test_objects.cpp           | 54 ++++++++++++++++++++++++++++++
 11 files changed, 348 insertions(+), 18 deletions(-)
```
