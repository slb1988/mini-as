# feat(addon): provide dictionary object

- Commit: 56dc8069efcff997af455306c5e8fcf90da02f50
- Date: 2026-08-11 22:05:14 +0800
- Author: sunlaibing

## 变更内容

标准 add-on 新增可被 GC 的 `dictionary` 引用类型，基于 VM 既有异构 `Value` 表示：

- 注册表面覆盖官方核心形态：`set`/`get` 的 `int64`/`double` 重载、内置 `string`/`bool` 重载、`exists`/`isEmpty`/`getSize`/`delete`/`deleteAll`/`getKeys`、`dictionaryValue` 及其官方 `int64 opConv()`、经注册 `get(string)` 协议的方括号读取、以及 foreach 用的 `opForBegin/End/Next` 与编号值/键方法；`RegisterScriptDictionary` 依赖 array 模板 add-on（镜像官方 `getKeys()` 依赖 `array<string>`）
- 底层 C++ `Set`/`Get` 可存任意 `Value`（含对象与函数 handle）；官方 `?&in`/`?&out` 声明留给通配符/变参阶段，此前不支持的脚本侧值类型走普通重载失败而非静默强转
- `RegisterObjectType` 新增可选 GC 标记：dictionary 实例注册进引擎收集器，枚举条目中的对象/函数/捕获引用，死环回收时清空全部条目
- 迭代使用稳定有序键映射与无符号隐藏迭代器；值拷入 `dictionaryValue`，键以 string 返回；转换失败定位于转换或方括号表达式
- 无 dictionary 专用 opcode：factory、方法、out 回写、转换、索引、foreach 全部复用既有带类型宿主调用描述符

## 相关文档

- [docs/stages/79-dictionary-object.md](../stages/79-dictionary-object.md)

## 涉及文件

```
 AGENTS.md                               |   8 +-
 CMakeLists.txt                          |  12 ++
 docs/stages/79-dictionary-object.md     |  64 ++++++++
 include/mini_as/addons/dictionary.hpp   |  42 ++++++
 include/mini_as/engine.hpp              |   2 +-
 src/engine.cpp                          |   5 +-
 src/script_dictionary.cpp               | 261 ++++++++++++++++++++++++++++++++
 src/type_checker.cpp                    |   3 +-
 tests/compat/cases/dictionary_object.as |  16 ++
 tests/compat/mini_runner.cpp            |   6 +-
 tests/compat/official_runner.cpp        |   9 +-
 tests/test_addons.cpp                   | 114 ++++++++++++++
 12 files changed, 534 insertions(+), 8 deletions(-)
```
