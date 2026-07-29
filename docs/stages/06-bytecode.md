# 06 - Typed bytecode and disassembly

The compiler now lowers checked AST nodes into a constant pool plus fixed-size
instructions. Locals use integer slots, assignment is `value, DUP, STORE`, and
numeric opcodes are typed (`ADD_I` versus `ADD_F`). The compiler inserts the
single supported implicit conversion explicitly as `TO_FLOAT`.

Every instruction keeps its source location. This costs more than AngelScript's
compressed line tables but makes the relationship between diagnostics,
debugging, suspension, and bytecode immediately visible. Jump operands are
absolute instruction indexes in this teaching format and will be backpatched
once their target is known.

The disassembler is a first-class debugging tool and is tested alongside the
compiler; later VM failures can always be compared with the exact instruction
stream they execute.
