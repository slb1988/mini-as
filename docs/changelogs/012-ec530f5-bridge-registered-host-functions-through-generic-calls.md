# feat: bridge registered host functions through generic calls

- Commit: ec530f5bd932e9ed2d234c7fbf702645d3e0ac87
- Date: 2026-07-30 01:10:45 +0800
- Author: sunlaibing

## 变更内容

实现 generic host calling bridge，把宿主函数注册接入编译管线：

- 注册声明文本被解析进与脚本共用的 `DataType` / 函数签名模型，因此编译期就能决议 host call 并插入类型转换
- `CALL_HOST` 只把已检查的值移入 `GenericCall`，调用类型擦除的回调，并校验声明的返回类型
- 刻意实现 AngelScript 的 portable generic convention 而非平台 ABI 桥接：不把 VM 值塞进 CPU 参数寄存器、不需要汇编，同时保留注册、重载匹配、异常传播与宿主隔离的语义可见性

测试覆盖 host 调用处的类型转换、宿主显式抛异常、非法声明、reference 风格声明文本与重复注册。

## 相关文档

- [docs/stages/11-generic-host-bridge.md](../stages/11-generic-host-bridge.md)

## 涉及文件

```
 CMakeLists.txt                        |  2 +
 docs/stages/11-generic-host-bridge.md | 15 ++++++
 include/mini_as/bytecode.hpp          |  4 ++
 include/mini_as/engine.hpp            |  6 ++-
 include/mini_as/generic.hpp           | 45 +++++++++++++++++
 src/bytecode.cpp                      | 21 ++++++--
 src/engine.cpp                        | 27 +++++++++-
 src/generic.cpp                       | 91 +++++++++++++++++++++++++++++++++++
 src/vm.cpp                            | 19 +++++++-
 tests/test_generic.cpp                | 45 +++++++++++++++++
 10 files changed, 267 insertions(+), 8 deletions(-)
```
