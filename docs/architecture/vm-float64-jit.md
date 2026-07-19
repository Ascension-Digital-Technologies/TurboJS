# VM Float64 Baseline JIT

TurboJS maintains separate baseline code caches for integer and Float64
specializations of the same bytecode function.

When all arguments are numeric and at least one argument is a Float64 value,
the VM records a Float64 hotness counter. Once the normal baseline threshold is
reached, the engine bytecode frontend lowers supported arithmetic operations to
Float64 IR and the x86-64 backend emits SSE2 scalar instructions.

Supported VM-specialized operations currently include argument loads, local
loads/stores, addition, subtraction, multiplication, division, and Float64
return. Unsupported opcodes fail closed to the interpreter.

The integer and Float64 caches are independent so warming one specialization
does not overwrite the other. Both are invalidated with the bytecode object and
destroyed with the runtime. Runtime statistics aggregate both caches.
