# 02 - Tokenizer

The tokenizer is a single forward state machine. It recognizes the longest
valid operator (`=` versus `==`), treats keywords as a post-classification of
identifiers, and records the location before consuming a token. Comments are
discarded but still advance row and column counters.

String scanning distinguishes an escaped quote from the closing quote. The
token retains the original lexeme; decoding is a parser/evaluator concern so
diagnostics can always quote the exact source spelling.

This mirrors AngelScript's hand-written tokenizer without its large literal
and portability surface. Running the lexer never throws for source errors: it
reports them and always emits an end token.
