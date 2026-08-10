
src/parser.cpp 里并没有「ParseOr 专用的宏」。真正存在的是一个通用宏 BINARY_LEVEL（parser.cpp:181-189），ParseOr 只是它的第一个实例化结果（parser.cpp:191）。宏体本身完全不认识 ParseOr：

#define BINARY_LEVEL(method, next, ...) \
AstNode* Parser::method() { \
AstNode* expression = next(); \
while (MatchAny({__VA_ARGS__})) { \
AstNode* node = arena_->Make(NodeKind::Binary, Previous()); \
node->AppendChild(expression); node->AppendChild(next()); expression = node; \
} \
return expression; \
}

BINARY_LEVEL(ParseOr, ParseAnd, TokenKind::OrOr)
BINARY_LEVEL(ParseAnd, ParseEquality, TokenKind::AndAnd)
BINARY_LEVEL(ParseEquality, ParseComparison, TokenKind::EqualEqual, TokenKind::BangEqual, TokenKind::KwIs)
BINARY_LEVEL(ParseComparison, ParseTerm, TokenKind::Less, TokenKind::LessEqual, TokenKind::Greater, TokenKind::GreaterEqual)
BINARY_LEVEL(ParseTerm, ParseFactor, TokenKind::Plus, TokenKind::Minus)
BINARY_LEVEL(ParseFactor, ParseUnary, TokenKind::Star, TokenKind::Slash, TokenKind::Percent)
#undef BINARY_LEVEL

展开后是什么

BINARY_LEVEL(ParseOr, ParseAnd, TokenKind::OrOr) 在预处理后就是一个普通的成员函数定义：

AstNode* Parser::ParseOr() {
    AstNode* expression = ParseAnd();           // 先解析更高优先级的左操作数
    while (MatchAny({TokenKind::OrOr})) {       // 只要下一个 token 是 ||，就吃掉它
        AstNode* node = arena_->Make(NodeKind::Binary, Previous());  // Previous() 就是刚吃掉的 ||
        node->AppendChild(expression);          // 左子树
        node->AppendChild(ParseAnd());          // 右子树，同样是更高优先级的一层
        expression = node;                      // 新节点变成新的左操作数 → 左结合
    }
    return expression;
}

函数声明是手写在 include/mini_as/parser.hpp:58-63 里的，宏只IDE 跳转、调用方都看不到宏的存在。

它解决的是什么问题

递归下降解析器处理二元运算符优先级时，每一层的文法产生式形状

Level_n  →  Level_{n+1}  ( op_n  Level_{n+1} )*

区别只有三样东西：本层函数名、下一层函数名、本层接受哪些运算得到六段除了三个标识符以外一模一样的代码，大约 50行纯重复。宏把「模式」抽出来，六行声明式的实例化列表就把整条优先级链表达清楚了，而且这张表本身就是可读的优先级文档：

ParseAssignment  (=, 右结合，手写)
└─ ParseOr        ||          ← 优先级最低
└─ ParseAnd       &&
└─ ParseEquality      == != is
└─ ParseComparison  < <= > >=
└─ ParseTerm      + -
└─ ParseFactor  * / %
└─ ParseUnary   ! - + @
└─ ParseCall    () .
└─ ParsePrimary  ← 绑定最

调用越深，绑定越紧。这就是 1 + 2 * 3 会解析成 1 + (2 * 3) 的数都必须先经过 ParseFactor，* 在下沉过程中就被消化掉了。

几个语法细节

为什么第三个参数用 ... 变参，而不是写成一个参数。 因为运算符INARY_LEVEL(method, next, ops)，那 BINARY_LEVEL(ParseTerm,ParseFactor, TokenKind::Plus, TokenKind::Minus) 会被预处理器当成传了 4 个参数而报错——预处理器不认识 {} 之外的结构，逗号一律是参数分隔符。用 ... + __VA_ARGS__ 就能把任意多个运算符原样搬进去。

MatchAny({__VA_ARGS__}) 那对花括号。 MatchAny 收的是 std::in parser.hpp:71），需要一个 braced-init-list才能构造。花括号写在宏体里而不是调用点，所以实例化处只需列出裸的 TokenKind::...，看起来更像一张表。

Previous() 为什么是运算符。 MatchAny → Match → Advance() 会让 current_ 前进一格（parser.cpp:267-273），所以 MatchAny 返回 true
之后，刚被消耗的那个运算符 token 正好落在 Previous()。这个 ttoken 字段，同时承载了「是哪个运算符」和「源码位置」——后续type_checker 和 bytecode 都靠 node->token.kind 分派（如 bytecode.cpp:182）。

左结合 vs 右结合。 宏里用的是 while 循环 + 重新赋值 expression = node，产生左结合：a - b - c → Binary(-, Binary(-, a, b), c)，语义是
(a-b)-c。对比紧挨着的 ParseAssignment（parser.cpp:172-179） ssignment() 自身，产生右结合：a = b = c → a = (b =c)。这也正是赋值没有被塞进宏的原因：结合性不同，套不进同一个模式。

#undef BINARY_LEVEL（parser.cpp:197）。 宏没有作用域也不受命名空间约束，一旦定义就会一直污染到翻译单元结束。用完立刻 undef
是良好卫生习惯，尤其能避免和后面的代码或其他头文件撞名。

一个需要注意的点

|| 和 && 经过这个宏生成的是普通的 NodeKind::Binary 节点，和 区别。短路求值语义不在 parser里体现，而是延后到编译阶段——bytecode.cpp:182 检测到 token.kind 是 AndAnd/OrOr 时改走 CompileLogical，用跳转指令实现短路。这个分工是合理的（parser
只管形状，语义留给后端），但读代码时容易误以为逻辑运算符会求

代价与替代方案

宏方案的代价主要在工具链体验上：六个函数在调试器里都指向 par那同一段源码，单步进去分不清自己在哪一层；宏体内的编译错误信息也会指向宏而不是实例化点。对这个规模（6 层、一次性、紧挨着 undef）完全可以接受。

如果想避免宏，常见的两条路：

- 模板 + 成员函数指针：template <AstNode* (Parser::*Next)(), TokenKind... Ops>，类型安全、可调试，但写法更绕，且每层还要一个转发函数。
- Pratt parser / precedence climbing：把优先级做成一张 Token的表，用一个函数带优先级参数递归，六个函数塌缩成一个。这是主流做法（Lua、Clang 的表达式解析都用类似思路），扩展新运算符时只改表不改代码，而。真要重构，这个方向收益更大。