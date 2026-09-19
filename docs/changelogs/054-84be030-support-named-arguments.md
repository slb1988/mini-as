# feat(functions): support named arguments

- Commit: 84be03079c71e371084685b361f03521d0083625
- Date: 2026-08-09 08:46:06 +0800
- Author: sunlaibing

## 变更内容

调用实参支持 AngelScript 的 `parameter: expression` 命名实参语法：

- 一旦出现命名实参，之后的位置实参被拒绝；命名实参可乱序，也可跳过有默认值的参数
- 参数名保留在 `FunctionSignature` 中；类型检查先把每个实参映射到唯一参数再计算转换成本——未知名字、重复名字、缺少必填参数都会使该重载不可行（non-viable）
- bytecode 重复同一映射，按参数槽位顺序发射值后再调用既有 callable 描述符
- 统一适用于 function、method、constructor 以及声明中含参数名的注册宿主函数

## 相关文档

- [docs/stages/41-named-arguments.md](../stages/41-named-arguments.md)

## 涉及文件

```
 CMakeLists.txt                        |  7 +++++++
 docs/stages/41-named-arguments.md     | 14 ++++++++++++++
 include/mini_as/parser.hpp            |  2 +-
 include/mini_as/type_checker.hpp      |  7 +++++--
 src/bytecode.cpp                      | 74 +++++++++++++++++++++++++++++++++++++++++++++++-----------
 src/generic.cpp                       |  3 +++
 src/parser.cpp                        | 16 +++++++++++++-
 src/type_checker.cpp                  | 93 +++++++++++++++++++++++++++++++++++++++++++++-----------------------------
 tests/compat/cases/named_arguments.as | 15 ++++++++++++
 tests/test_bytecode.cpp               | 19 +++++++++++++++
 tests/test_engine.cpp                 | 32 ++++++++++++++++++++++++++
 tests/test_parser.cpp                 | 18 +++++++++++++++
 tests/test_types.cpp                  | 13 +++++++++++
 13 files changed, 255 insertions(+), 58 deletions(-)
```
