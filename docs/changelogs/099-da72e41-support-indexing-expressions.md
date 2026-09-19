# feat(language): support indexing expressions

- Commit: da72e41804303b564bdd2dff427de9b9e05b4b36
- Date: 2026-08-11 21:42:57 +0800
- Author: sunlaibing

## 变更内容

后缀索引成为一等表达式与 lvalue：`values[1] = 10;`、`values[next()]++;`、`int old = values[0];`

- 方括号 token 与 `Index` AST 节点追加在枚举末尾，保持 version-2 bytecode 的既有 token/node 数值稳定
- **注册索引协议**：类型检查经两个注册方法决议索引——读用 `T get(uint index) const`，写用 `T set(uint index, T value)`（setter 返回被存值使赋值表达式保留结果）；array add-on 暴露该协议，但 parser/type-checker/compiler 并不认识 array 类型名
- 不可索引 receiver、不兼容索引类型、缺失 setter、getter/setter 元素类型不一致均为编译错误；运行时边界检查仍是 add-on 的职责并保留方括号表达式位置
- **lvalue 降级**：`LValueRef::Index` 完整实现——简单读写发射普通宿主方法调用；复合赋值与前/后缀自增把 receiver 与转换后的索引缓存进隐藏 local 再调 `get`/`set`，保证 `values[next()]++` 中 `next()` 只求值一次且保持前/后缀结果语义；未引入数组专用 opcode，未来实现同协议的注册类型自动可用

## 相关文档

- [docs/stages/77-indexing-expressions.md](../stages/77-indexing-expressions.md)

## 涉及文件

```
 AGENTS.md                                  |   9 +-
 CMakeLists.txt                             |   7 +++
 docs/stages/77-indexing-expressions.md     |  53 ++++++++
 include/mini_as/bytecode.hpp               |   6 +
 include/mini_as/parser.hpp                 |   2 +-
 include/mini_as/tokenizer.hpp              |   3 +-
 include/mini_as/type_checker.hpp           |   1 +
 src/bytecode.cpp                           | 207 ++++++++++++++++++++++++++++-
 src/bytecode_io.cpp                        |   2 +-
 src/parser.cpp                             |   6 +
 src/script_array.cpp                       |   6 +-
 src/tokenizer.cpp                          |   7 +-
 src/type_checker.cpp                       |  40 +++++-
 tests/compat/cases/indexing_expressions.as |  10 ++
 tests/compat/mini_runner.cpp               |   3 +-
 tests/compat/official_runner.cpp           |   3 +-
 tests/test_addons.cpp                      |  58 ++++++++
 tests/test_parser.cpp                      |  13 ++
 tests/test_tokenizer.cpp                   |   9 ++
 19 files changed, 423 insertions(+), 22 deletions(-)
```
