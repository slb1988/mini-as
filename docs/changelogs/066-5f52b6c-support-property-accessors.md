# feat(classes): support property accessors

- Commit: 5f52b6c86f294f2303549634a1fc5f95769e6b19
- Date: 2026-08-09 17:04:25 +0800
- Author: sunlaibing

## 变更内容

script class 与 interface 支持虚属性访问器，两种官方声明形式均被接受：

- 紧凑形式 `int value { get const; set; }`，以及显式命名的 `get_value`/`set_value` 加 `property` 装饰器；仅仅名字以 `get_`/`set_` 开头不算属性
- parser 把紧凑声明展开为普通访问器方法 AST（setter 获得隐式参数 `value`）；访问器在类型元数据中被标记，但继承与 interface 方法仍占用与普通方法相同的稳定虚槽
- 类型检查要求 getter 无参数且返回非 void、setter 返回 void 且单参数，配对类型必须一致；索引访问器在此诊断并留给后续索引阶段
- 读 `object.value` 降级为 `object.get_value()`，赋值降级为 `object.set_value(result)`，方法内省略 `this.` 同样生效；interface handle 走既有 `CallVirtual` 分派；只读/只写/畸形/未标记/不可达属性各有独立诊断
- 复合赋值只求值一次 receiver（存入隐藏 local），调 getter、算新值、调 setter；被赋值仍是表达式结果；与 AngelScript 2.38.0 一致，虚属性拒绝自增自减；对象值复合属性留待值对象模型
- 全局与索引属性访问器不在本阶段

## 相关文档

- [docs/stages/49-property-accessors.md](../stages/49-property-accessors.md)

## 涉及文件

```
 AGENTS.md                                |   5 +++--
 CMakeLists.txt                           |   7 +++++++
 docs/stages/49-property-accessors.md     |  35 +++++++++++++++++++++++++++++++
 include/mini_as/bytecode.hpp             |   1 +
 include/mini_as/parser.hpp               |   3 +++
 include/mini_as/type_checker.hpp         |   3 +++
 src/bytecode.cpp                         |  94 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 src/parser.cpp                           |  35 +++++++++++++++++++++++++++++++
 src/type_checker.cpp                     | 173 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-----
 tests/compat/cases/property_accessors.as |  35 +++++++++++++++++++++++++++++++
 tests/test_bytecode.cpp                  |  29 ++++++++++++++++++++++++++++
 tests/test_engine.cpp                    |  74 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 tests/test_parser.cpp                    |  19 ++++++++++++++++
 tests/test_tokenizer.cpp                 |   9 ++++++++
 14 files changed, 501 insertions(+), 21 deletions(-)
```
