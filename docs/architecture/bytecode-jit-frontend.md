# Bytecode-to-JIT Frontend

TurboJS now includes a compact, validated bytecode frontend that translates portable
TurboJS bytecode into the shared register IR. The frontend is intentionally separate
from the legacy interpreter decoder: this keeps native compilation testable while the
full JavaScript bytecode bridge is introduced opcode-by-opcode.

## Pipeline

```text
TurboJS portable bytecode
        -> bytecode verifier / translator
        -> TurboJS IR verifier
        -> reference IR interpreter OR x86-64 baseline compiler
```

The baseline compiler uses stack-backed virtual registers. All 64 IR registers are
therefore available without tying the IR to the host register count. Generated code
currently lowers integer constants and arguments, arithmetic, signed comparisons,
unconditional jumps, conditional branches, loops, and returns.

Branch targets are recorded as instruction indices and patched after emission. Invalid
registers and targets are rejected before executable memory is allocated. Unsupported
future opcodes return `TURBOJS_IR_UNSUPPORTED` and remain eligible for interpreter
fallback.

## Current boundary

This frontend is the stable bridge for AOT fixtures and JIT tests. The next integration
step maps the engine's existing JavaScript bytecode (`JSFunctionBytecode`) into the same
IR while preserving generic-value fallback for dynamic operations.
