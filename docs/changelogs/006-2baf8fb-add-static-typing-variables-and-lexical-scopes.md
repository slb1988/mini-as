# feat: add static typing variables and lexical scopes

- Commit: 2baf8fbc56152f3de18900ffe45cf4b5a4062dc7
- Date: 2026-07-30 01:00:18 +0800
- Author: sunlaibing

## 变更内容

引入静态类型检查与词法作用域：

- 类型检查在 bytecode 出现之前注解每个表达式节点；scope stack 把名字映射到 `DataType`，查找从最内层 block 向外走，而声明只检查当前 block——因此 shadowing 合法、重复声明报错
- function 先预声明再检查函数体；每个表达式向父节点返回类型，相当于 AngelScript expression context 的简化版
- 唯一的通用隐式转换是 `int` → `float`；字符串拼接有显式的格式化规则（为教程兼容性而设）

同时把测试重构为按 feature 分文件注册（test_core/test_tokenizer/test_parser/test_types/test_interpreter），此后每个行为都随同一 commit 附带正向测试与相关的编译期/运行时失败测试。

## 相关文档

- [docs/stages/05-static-types-and-scopes.md](../stages/05-static-types-and-scopes.md)

## 涉及文件

```
 CMakeLists.txt                            |  10 +-
 docs/stages/05-static-types-and-scopes.md |  14 ++
 include/mini_as/type_checker.hpp          |  48 ++++++
 src/type_checker.cpp                      | 238 ++++++++++++++++++++++++++++++
 tests/test.hpp                            |  22 +++
 tests/test_core.cpp                       |  15 ++
 tests/test_interpreter.cpp                |  14 ++
 tests/test_main.cpp                       |  58 +++-----
 tests/test_parser.cpp                     |  18 +++
 tests/test_tokenizer.cpp                  |  22 +++
 tests/test_types.cpp                      |  24 +++
 11 files changed, 444 insertions(+), 39 deletions(-)
```
