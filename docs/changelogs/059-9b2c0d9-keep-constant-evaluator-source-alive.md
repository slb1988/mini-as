# fix(tests): keep constant evaluator source alive

- Commit: 9b2c0d97cb96885f39bf0500037c265f264aef66
- Date: 2026-08-09 10:22:24 +0800
- Author: sunlaibing

## 变更内容

修复测试中的悬垂 `string_view`：`Tokenizer` 以 `std::string_view` 引用源码，此前直接把临时拼接的 string 传入，临时对象析构后 tokenizer 读到的是已释放内存。改为先把源码存入具名 `const std::string` 再构造 tokenizer。

## 涉及文件

```
 tests/test_constant_evaluator.cpp | 3 ++-
 1 file changed, 2 insertions(+), 1 deletion(-)
```
